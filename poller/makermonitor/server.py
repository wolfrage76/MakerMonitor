from __future__ import annotations

import asyncio
import os
import re
import logging
import socket
from logging.handlers import RotatingFileHandler
import time
from typing import Any

from fastapi import Cookie, Depends, FastAPI, Header, HTTPException, Query
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel

from .color import ColorPanelClient, build_state, color_events
from .config import Settings, config_path
from .creator import CreatorStats
from .daily import DayTracker
from .dashboard import DASHBOARD_HTML, LOGIN_HTML
from .makerworld import AuthError, MakerWorldClient
from .pages import build_sections
from .notify import NotifyHub
from .panel import PanelClient, PanelGroup, build_events
from .panels import PanelDirectory
from .ratelimit import LIMITER
from . import settings_api
from .state import Snapshot, Store

log = logging.getLogger(__name__)

# Seconds between retries when polls fail right after startup.
STARTUP_RETRIES = (15, 30, 60, 120)
# After a failed poll later on (a MakerWorld blip): retry sooner, then back
# to the normal interval.
FAIL_RETRIES = (120, 300)
POLL_TIMEOUT_SEC = 180.0
DEAD_PANEL_PROBE_SEC = 60.0
PANEL_DOWN_AFTER = 2       # failed checks in a row before a panel counts as down
PANEL_RECENT_SEC = 90.0    # a panel that answered this recently still gets every push


def setup_logging(log_path) -> None:
    """File plus the existing console handlers. httpx access lines stay out of the way."""
    log_path.parent.mkdir(parents=True, exist_ok=True)
    root = logging.getLogger()
    root.setLevel(logging.INFO)
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
    path = str(log_path.resolve())
    if not any(getattr(h, "baseFilename", None) == path for h in root.handlers):
        fh = RotatingFileHandler(path, maxBytes=1_000_000, backupCount=5)
        fh.setFormatter(fmt)
        root.addHandler(fh)
    logging.getLogger("httpx").setLevel(logging.WARNING)
    logging.getLogger("httpcore").setLevel(logging.WARNING)
    logging.getLogger("uvicorn.access").setLevel(logging.WARNING)


def _cookie_names(header: str) -> str:
    names = []
    for part in header.split(";"):
        name = part.split("=", 1)[0].strip()
        if name:
            names.append(name)
    return ",".join(names[:16]) or "none"


def _event_summary(events: list[dict[str, Any]], limit: int = 6) -> str:
    if not events:
        return "none"
    bits = []
    for e in events[:limit]:
        kind = e.get("k") or e.get("type") or "?"
        n = e.get("n", e.get("value", 1))
        model = e.get("m") or e.get("modelName") or ""
        bits.append(f"+{n} {kind} {model}".strip())
    extra = len(events) - limit
    if extra > 0:
        bits.append(f"+{extra} more")
    return "; ".join(bits)


class CookieIn(BaseModel):
    cookie: str | None = None
    cookies: dict[str, str] | None = None
    source: str = "extension"


def cookie_header_from(body: CookieIn) -> str:
    if body.cookie and body.cookie.strip():
        raw = body.cookie.strip()
        if raw.lower().startswith("cookie:"):
            raw = raw.split(":", 1)[1].strip()
        if "=" not in raw:
            return f"token={raw}"
        return raw
    if body.cookies:
        return "; ".join(f"{k}={v}" for k, v in body.cookies.items() if v)
    raise HTTPException(400, "Provide cookie string or cookies map")


class AppContext:
    def __init__(self, settings: Settings) -> None:
        self.settings = settings
        self.store = Store(settings.data_path)
        # One `panels` list; each host's kind (color / e-paper) is detected
        # and cached (panels.py). Until a host has answered once it gets nothing.
        self.panel_dir = PanelDirectory(settings.data_path)
        self.panel = PanelGroup([])
        self.colors: list[ColorPanelClient] = []
        self._probes: dict[str, ColorPanelClient] = {}
        self._panel_up: dict[str, bool] = {}
        self._panel_next_probe: dict[str, float] = {}
        self._panel_misses: dict[str, int] = {}
        self._panel_ok_at: dict[str, float] = {}
        self._rebuild_panels()
        self.day = DayTracker(settings.data_path)
        # Any panel's Settings menu (e-ink or color) can override the interval.
        self.poll_interval = settings.poll_interval_sec
        self._color_seen: dict[str, tuple[int, Any]] = {}  # base -> (pollSec, pollNow seq)
        self._poll_lock = asyncio.Lock()
        self._task: asyncio.Task | None = None
        self._watchdog_task: asyncio.Task | None = None
        self._loop_tick = time.time()
        self._wake_event = asyncio.Event()
        self._poll_requested = False
        # Per-source fetch health for the panels and the dashboard.
        self.health: dict[str, dict[str, Any]] = {k: {"t": 0.0, "fails": 0, "err": ""}
                                                  for k in ("models", "reach", "points")}
        # Creator stats (background, low priority); its health is its own source.
        LIMITER.load(settings.data_path)
        self.creator = CreatorStats(settings.data_path)
        self.health["model-stats"] = self.creator.health
        self.auth_state = "none"
        self._auth_fails = 0  # polls refused in a row (401/403)
        self._health_client: Any = None
        self._self_url = ""
        self.last_state: dict[str, Any] | None = None
        self.notifier = NotifyHub(settings.notifications, settings.handle)
        snap = self.store.status.last_snapshot
        if snap and snap.fetched_at:
            try:
                auth = "ok" if self.store.status.cookie_present else "needed"
                st = build_state(snap, self.day, self._color_status(auth, "synced"), self.poll_interval)
                st["health"] = self.health_view()
                st["insights"] = self.day.insights()
                st["poller"] = self.self_url()
                full = self.creator_full()
                if full:
                    st["creator"] = self.creator.compact(full)
                self.last_state = st
            except Exception:  # noqa: BLE001
                pass

    def update_health(self, client: MakerWorldClient, cookie: bool) -> None:
        """Fold one poll's per-source results in (once per poll)."""
        if client is self._health_client:
            return
        self._health_client = client
        now = time.time()
        for src, h in self.health.items():
            r = client.results.get(src)
            if r is None:
                continue  # not attempted this poll (e.g. no cookie)
            if r == "ok":
                h.update(t=now, fails=0, err="")
            else:
                h["fails"] += 1
                h["err"] = "rejected (401/403)" if r == "auth" else r
        if not cookie:
            self.auth_state = "none"
        # The model list only answers with a valid cookie, but single requests get
        # refused now and then; point-bill and data-overview (reach) are often
        # refused on their own, so they don't count. Expired = AUTH_REFUSALS
        # refused polls in a row.
        elif self._auth_fails >= AUTH_REFUSALS:
            self.auth_state = "expired"
        elif self._auth_fails == 0 and client.results.get("models") == "ok":
            self.auth_state = "ok"

    def _rebuild_panels(self) -> None:
        """Color clients and the e-paper group from the detected kinds."""
        bases = self.settings.panel_bases
        have = {p.base_url: p for p in self.colors}
        have_eink = {c.base_url: c for c in self.panel.clients}
        self.colors = [have.get(b) or ColorPanelClient(b) for b in bases if self.panel_dir.kind(b) == "color"]
        self.panel = PanelGroup([have_eink.get(b) or PanelClient(b) for b in bases if self.panel_dir.kind(b) == "eink"])
        self._probes = {b: self._probes.get(b) or ColorPanelClient(b) for b in bases}

    async def refresh_panels(self, force: bool = False) -> None:
        """Detect hosts not seen yet (or due a re-check) and regroup."""
        bases = self.settings.panel_bases
        before = {b: self.panel_dir.kind(b) for b in bases}
        if bases:
            await asyncio.gather(*(self.panel_dir.check(b, force=force) for b in bases))
        after = {b: self.panel_dir.kind(b) for b in bases}
        if after != before or force:
            self._rebuild_panels()
            log.info("panels: color=%s eink=%s unknown=%s",
                     ",".join(p.base_url for p in self.colors) or "none",
                     self.panel.base_url or "none",
                     ",".join(b for b, k in after.items() if k == "unknown") or "none")

    def panels_view(self) -> list[dict[str, Any]]:
        return self.panel_dir.view(self.settings.panel_bases, self._panel_up)

    def apply_settings(self, new: Settings) -> None:
        """Live-apply a config saved from the dashboard's Settings tab.

        Fields are set on the existing Settings object so everything holding
        it sees them. listen_*, data_dir and api_token need a restart.
        """
        old = self.settings
        if new.poll_interval_sec != old.poll_interval_sec:
            self.poll_interval = new.poll_interval_sec
        for name in type(new).model_fields:
            if name not in ("listen_host", "listen_port", "data_dir", "api_token"):
                setattr(old, name, getattr(new, name))
        self._rebuild_panels()
        try:
            asyncio.get_running_loop().create_task(self.refresh_panels())
        except RuntimeError:
            pass
        live = set(old.panel_bases)
        self._color_seen = {k: v for k, v in self._color_seen.items() if k in live}
        self._panel_up = {k: v for k, v in self._panel_up.items() if k in live}
        self._panel_next_probe = {k: v for k, v in self._panel_next_probe.items() if k in live}
        self._self_url = ""
        # New hub, same health state and held messages per channel name.
        hub = NotifyHub(old.notifications, old.handle)
        prev = self.notifier
        hub.health, hub._tasks, hub._announced = prev.health, prev._tasks, prev._announced
        held = {c.name: c for c in prev.channels}
        for c in hub.channels:
            if c.name in held:
                p = held[c.name]
                c.held, c.quiet_held, c._was_quiet = p.held, p.quiet_held, p._was_quiet
        self.notifier = hub
        log.info("settings applied: interval=%ss panels=%s notify=%s", self.poll_interval,
                 ",".join(old.panel_hosts) or "none",
                 ",".join(c.name for c in hub.channels) or "none")

    def health_view(self) -> dict[str, Any]:
        m = self.health["models"]
        stale = bool(m["t"]) and time.time() - m["t"] > 3 * self.poll_interval or (not m["t"] and m["fails"] > 0)
        # The poll loop wakes every 15 s; no wake for 3 poll intervals means it stopped.
        tick = getattr(self, "_loop_tick", 0.0)
        return {
            "auth": self.auth_state,
            "stale": stale,
            "loopStalled": bool(tick) and time.time() - tick > 3 * self.poll_interval,
            "loopTick": int(tick),
            "since": int(m["t"]),
            "src": {k: {"t": int(h["t"]), "f": h["fails"], "e": h["err"][:40]} for k, h in self.health.items()},
            # All MakerWorld requests from this poller (ratelimit.py).
            "limiter": LIMITER.view(),
            # Each configured panel: host, detected kind, board, firmware, answering.
            "panels": self.panels_view(),
        }

    def creator_full(self) -> dict[str, Any] | None:
        snap = self.store.status.last_snapshot
        if not snap or not snap.models:
            return None
        try:
            return self.creator.compute(snap, self.day.view(), list(self.day.d.get("days", [])))
        except Exception as exc:  # noqa: BLE001 - never break the panels' state
            log.exception("creator stats failed: %s", exc)
            return None

    def self_url(self) -> str:
        """This poller as seen from the panels' network (for lazy fetches)."""
        if not self._self_url:
            ip = "127.0.0.1"
            hosts = [b.split("//", 1)[-1].split(":")[0] for b in self.settings.panel_bases]
            # Use a numeric target so UDP route lookup never blocks on mDNS/DNS.
            target = next((h for h in hosts if re.match(r"^\d+(?:\.\d+){3}$", h)), "192.168.1.1")
            for dst in (target, "192.168.1.1", "8.8.8.8"):
                try:
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sk:
                        sk.connect((dst, 80))
                        cand = sk.getsockname()[0]
                        if cand and not cand.startswith("127."):
                            ip = cand
                            break
                except OSError:
                    continue
            self._self_url = f"http://{ip}:{self.settings.listen_port}"
        return self._self_url

    def request_poll(self) -> None:
        """Schedule an early poll and coalesce bursts (e.g. multiple cookie updates)."""
        self._poll_requested = True
        self._wake_event.set()

    async def start(self) -> None:
        self._loop_tick = time.time()
        self._task = asyncio.create_task(self._poll_loop(), name="poll-loop")
        self._task.add_done_callback(self._loop_done)
        self._watchdog_task = asyncio.create_task(self._watchdog_loop(), name="poll-watchdog")

    async def _watchdog_loop(self) -> None:
        while True:
            await asyncio.sleep(60)
            limit = max(4 * self.poll_interval, 600)
            age = time.time() - self._loop_tick
            if age > limit:
                log.critical("poll loop stalled for %.0fs (limit %ds); exiting for service restart", age, limit)
                os._exit(1)

    def _loop_done(self, task: asyncio.Task) -> None:
        """The poll loop must never stop on its own: log why it did and start it again."""
        if task.cancelled() or task is not self._task:
            return
        exc = task.exception()
        log.error("poll loop stopped (%s); restarting it in 30 s", f"{type(exc).__name__}: {exc}" if exc else "returned",
                  exc_info=exc)

        async def again() -> None:
            await asyncio.sleep(30)
            if self._task is task:
                self._task = asyncio.create_task(self._poll_loop(), name="poll-loop")
                self._task.add_done_callback(self._loop_done)

        asyncio.get_running_loop().create_task(again())

    async def stop(self) -> None:
        for t in (self._watchdog_task, self._task):
            if t:
                t.cancel()
                try:
                    await t
                except asyncio.CancelledError:
                    pass

    async def _poll_loop(self) -> None:
        # Small delay so HTTP server is up first
        await asyncio.sleep(2)
        if self.settings.legacy_panel_keys:
            log.warning("config: %s still work but are deprecated; list every panel under `panels:` instead "
                        "(the dashboard's Settings tab does this when you save)", ", ".join(self.settings.legacy_panel_keys))
        try:
            await asyncio.wait_for(self.refresh_panels(force=True), timeout=30.0)
        except Exception as exc:  # noqa: BLE001 - a panel problem must not stop polling
            log.exception("panel detection failed: %s", exc)
        last = 0.0
        # Right after boot the network or cookie is often not ready yet, so
        # failed polls retry on a short schedule until one succeeds. Requests
        # still go through LIMITER, so its hourly cap and backoff apply.
        retries: list[int] | None = list(STARTUP_RETRIES)
        fail_retries = list(FAIL_RETRIES)
        wait: int | None = None
        while True:
            self._loop_tick = time.time()
            if self._poll_requested or time.time() - last >= (wait or self.poll_interval):
                self._poll_requested = False
                last = time.time()
                wait = None
                ok = False
                try:
                    await asyncio.wait_for(self.refresh_panels(), timeout=30.0)  # new hosts, and the 6-hourly re-check
                    ok = bool((await asyncio.wait_for(self.poll_once(), timeout=POLL_TIMEOUT_SEC)).get("ok"))
                except asyncio.TimeoutError:
                    log.error("Poll timed out after %.0fs", POLL_TIMEOUT_SEC)
                except Exception as exc:  # noqa: BLE001
                    log.exception("Poll loop error: %s", exc)
                if ok:
                    retries = None
                    fail_retries = list(FAIL_RETRIES)
                elif retries:
                    wait = retries.pop(0)
                    log.info("startup poll failed; retrying in %ss (%d retries left)", wait, len(retries))
                elif retries is not None:
                    retries = None
                    log.info("startup retries used up; back to the %ss interval", self.poll_interval)
                elif fail_retries:
                    wait = fail_retries.pop(0)
                    log.info("poll failed; trying again in %ss", wait)
            # Wake every 15 s (or after a short 2 s coalesce when request_poll() fires) to pick up panel settings / "poll now".
            if self._poll_requested:
                await asyncio.sleep(2.0)
                continue
            sleep_for = max(1.0, min(15.0, last + (wait or self.poll_interval) - time.time()))
            self._wake_event.clear()
            if not self._poll_requested:
                try:
                    await asyncio.wait_for(self._wake_event.wait(), timeout=sleep_for)
                except asyncio.TimeoutError:
                    pass
            if self._poll_requested:
                await asyncio.sleep(2.0)
                continue
            self._loop_tick = time.time()
            try:
                if await self._check_color_settings():
                    last = 0.0
            except Exception as exc:  # noqa: BLE001 - an odd panel answer must not stop polling
                log.exception("panel settings check failed: %s", exc)

    def _panel_answer(self, base: str, ok: bool) -> bool:
        """Record one check or push. A single miss (often a slow .local lookup)
        doesn't count; PANEL_DOWN_AFTER in a row mark the panel down.
        Returns True when the panel is now counted as down."""
        now = time.time()
        if ok:
            self._panel_misses[base] = 0
            self._panel_ok_at[base] = now
            self._panel_up[base] = True
            self._panel_next_probe.pop(base, None)
            return False
        misses = self._panel_misses.get(base, 0) + 1
        self._panel_misses[base] = misses
        if misses < PANEL_DOWN_AFTER and self._panel_up.get(base) is not False:
            return False
        self._panel_up[base] = False
        self._panel_next_probe[base] = now + DEAD_PANEL_PROBE_SEC
        return True

    async def _push_recovered_panel(self, base: str) -> None:
        """Push the latest cached state immediately when a panel comes online or reboots."""
        kind = self.panel_dir.kind(base)
        if kind == "color" and self.last_state:
            panel = next((p for p in self.colors if p.base_url == base), None)
            if panel:
                ok = await panel.state(self.last_state)
                log.info("color %s recovery state %s", base, "ok" if ok else "FAILED")
        elif kind == "eink":
            snap = self.store.status.last_snapshot
            client = next((c for c in self.panel.clients if c.base_url == base), None)
            if client and snap and snap.fetched_at:
                client.up = True
                ok = await client.sync(snap)
                await client.pages(build_sections(snap, self.settings.page_cycle_sec, self.day.view(),
                                                  insights=self.day.insights(),
                                                  model_history=self.day.model_history))
                log.info("eink %s recovery sync %s", base, "ok" if ok else "FAILED")

    async def _check_color_settings(self) -> bool:
        """Returns True when a panel (e-ink or color) asked for an immediate poll.

        The poll interval follows whichever panel's setting changed last.
        """
        poll_now = False
        regroup = False
        now = time.time()
        # Probe reachable hosts every 15 s; unreachable hosts back off to 60 s so
        # dead DNS/TCP timeouts don't stall every wake cycle.
        probes = [p for p in list(self._probes.values())
                  if now >= self._panel_next_probe.get(p.base_url, 0.0)]
        if not probes:
            return False
        results = await asyncio.gather(*(p.settings() for p in probes))
        recovered: list[str] = []
        for panel, cfg in zip(probes, results):
            base = panel.base_url
            was_up = self._panel_up.get(base)
            if not cfg:
                if not self._panel_answer(base, False):
                    continue  # one miss: check again on the next wake
                down = self._color_seen.get(base)
                if down != ("down", None):
                    log.warning("panel %s settings unreachable", base)
                    self._color_seen[base] = ("down", None)
                continue
            self._panel_answer(base, True)
            # A host that answers for the first time (or changed firmware)
            # joins the right group straight away.
            regroup |= self.panel_dir.note_board(base, cfg)
            sec = int(cfg.get("pollSec") or 0)
            seq = cfg.get("pollNow")
            seen = self._color_seen.get(base)
            if seen is None:
                log.info("Panel %s (%s): pollSec=%s pollNow=%s", base, cfg.get("board", "?"), sec, seq)
            if sec and 60 <= sec <= 3600 and (seen is None or seen[0] != sec) and sec != self.poll_interval:
                log.info("Poll interval %ss -> %ss (%s)", self.poll_interval, sec, base)
                self.poll_interval = sec
            rebooted = (seen not in (None, ("down", None))
                        and isinstance(seq, int) and isinstance(seen[1], int) and seq < seen[1])
            if seen not in (None, ("down", None)) and seq != seen[1] and not rebooted:
                poll_now = True
            if not was_up or rebooted:
                recovered.append(base)
            self._color_seen[base] = (sec, seq)
        if regroup:
            self._rebuild_panels()
            log.info("panels: color=%s eink=%s", ",".join(p.base_url for p in self.colors) or "none",
                     self.panel.base_url or "none")
        if recovered:
            await asyncio.gather(*(self._push_recovered_panel(b) for b in recovered), return_exceptions=True)
        return poll_now

    def _color_status(self, auth: str, message: str) -> dict[str, Any]:
        st = self.store.status
        age = int(time.time() - st.cookie_updated_at) if st.cookie_updated_at else -1
        return {"auth": auth, "msg": message, "cookieAge": age, "lastPoll": int(st.last_poll_at),
                "ok": st.last_poll_ok}

    async def _push_color(self, snap: Snapshot, events: list[dict[str, Any]], auth: str, message: str) -> None:
        client = getattr(self, "_poll_client", None)
        if client is not None:
            self.update_health(client, self._poll_cookie)
        try:
            state = build_state(snap, self.day, self._color_status(auth, message), self.poll_interval)
            state["health"] = self.health_view()
            state["insights"] = self.day.insights()
            state["poller"] = self.self_url()
            full = self.creator_full()
            if full:
                state["creator"] = self.creator.compact(full)
            self.last_state = state
        except Exception as exc:  # noqa: BLE001 - never break the e-ink path
            log.exception("Color state build failed: %s", exc)
            return
        if not self.colors:
            return

        async def _send_one(panel: ColorPanelClient) -> None:
            # Skip only a panel that is down and hasn't answered for a while; one
            # that answered recently still gets this poll's state and banners.
            base = panel.base_url
            if self._panel_up.get(base) is False and time.time() - self._panel_ok_at.get(base, 0.0) > PANEL_RECENT_SEC:
                return
            try:
                ok = await panel.state(state)
                self._panel_answer(base, ok)
                failed = 0
                for ev in events:
                    if not await panel.event(ev):
                        failed += 1
                    await asyncio.sleep(0.2)
                if events and failed:
                    log.warning("color %s state %s events %d/%d failed", panel.base_url, "ok" if ok else "FAILED", failed, len(events))
                else:
                    log.info("color %s state %s events %d", panel.base_url, "ok" if ok else "FAILED", len(events))
            except Exception as exc:  # noqa: BLE001 - never break the e-ink path
                log.exception("color %s push failed: %s", panel.base_url, exc)

        await asyncio.gather(*(_send_one(p) for p in self.colors))

    async def poll_once(self) -> dict[str, Any]:
        async with self._poll_lock:
            # Background MakerWorld fetches wait while the core poll runs.
            LIMITER.core_active += 1
            try:
                return await self._poll_once_unlocked()
            finally:
                LIMITER.core_active -= 1

    async def _poll_once_unlocked(self) -> dict[str, Any]:
        settings = self.settings
        store = self.store
        cookie = store.get_cookie()
        client = MakerWorldClient(settings.handle, settings.user_agent, cookie)
        self._poll_client, self._poll_cookie = client, bool(cookie)
        prev = store.status.last_snapshot
        result: dict[str, Any] = {"ok": False}

        store.status.panel_reachable = await self.panel.ping() or any(self._panel_up.values())
        started = time.time()
        age = int(time.time() - store.status.cookie_updated_at) if store.status.cookie_updated_at else None
        log.info(
            "poll start cookie=%s age=%s eink=%s color=%s",
            "yes" if cookie else "no",
            _fmt_age(age) if age is not None else "none",
            "up" if store.status.panel_reachable else "down",
            ",".join(p.base_url for p in self.colors) or "none",
        )

        try:
            snap = await client.fetch_snapshot(prev)
            if not snap.account_name:
                snap.account_name = settings.handle
            # The profile-page fallback (used when the design API fails, e.g. a
            # MakerWorld 502) has no per-model data and totals that don't match
            # the per-model sums. Accepting it after full data made every
            # count jump and sent bogus events; keep the last full snapshot.
            if prev.models and not snap.models:
                raise RuntimeError(f"partial data ({snap.source}): keeping the last full snapshot")
            _carry_ratings(prev, snap)
            store.status.auth_ok = bool(cookie)
            store.status.last_poll_ok = True
            # A model list that answered proves the cookie works, so it resets the
            # count; point-bill is refused on its own like reach and doesn't count.
            # Only a refused model list (profile fallback) counts as refused.
            refused = client.results.get("models") == "auth"
            self._auth_fails = self._auth_fails + 1 if refused else 0
            if refused and self._auth_fails < AUTH_REFUSALS:
                log.warning("poll partly refused (%d of %d in a row before the cookie counts as expired)",
                            self._auth_fails, AUTH_REFUSALS)
            store.status.last_error = ""
            store.status.last_poll_at = time.time()
            store.save_snapshot(snap)
            self.day.update(snap)
            # Re-baseline silently instead of replaying old activity as new events:
            # after a long gap (poller or MakerWorld down) and for any stat the
            # previous snapshot didn't have yet (all zero before, data now).
            stale = bool(prev.fetched_at) and snap.fetched_at - prev.fetched_at > 2 * self.poll_interval + 60
            fresh = _fresh_fields(prev, snap)
            if stale or fresh:
                log.info("Re-baselining without events (stale=%s, new fields=%s)", stale, sorted(fresh))
            cevents = color_events(prev, snap) if prev.fetched_at and not stale else []
            cevents = [e for e in cevents if _COLOR_FIELD.get(e.get("k")) not in fresh]
            self.day.add_events(cevents)

            if not prev.fetched_at or stale:
                # First successful snapshot → baseline sync, no spam notifications
                ok = await self.panel.sync(snap)
                log.info("eink %s sync %s", self.panel.base_url, "ok" if ok else "FAILED")
                result["action"] = "initial_sync"
            else:
                events = [e for e in build_events(prev, snap) if _EINK_FIELD.get(e.get("type")) not in fresh]
                if events:
                    failed = 0
                    for ev in events:
                        if not await self.panel.notify(ev):
                            failed += 1
                        await asyncio.sleep(0.3)
                    log.info(
                        "eink %s notify %d/%d (%s)",
                        self.panel.base_url,
                        len(events) - failed,
                        len(events),
                        _event_summary(events),
                    )
                    result["action"] = f"notified:{len(events)}"
                elif _totals_changed(prev, snap):
                    ok = await self.panel.sync(snap)
                    log.info("eink %s sync %s", self.panel.base_url, "ok" if ok else "FAILED")
                    result["action"] = "sync_totals"
                else:
                    result["action"] = "unchanged"

            # Sent every poll so a rebooted panel gets its sections back.
            self.update_health(client, bool(cookie))
            await self.panel.pages(build_sections(snap, settings.page_cycle_sec, self.day.view(),
                                                  insights=self.day.insights(),
                                                  model_history=self.day.model_history))

            age = int(time.time() - store.status.cookie_updated_at) if store.status.cookie_updated_at else None
            hv = self.health_view()
            if not cookie:
                await self.panel.status("needed", "public only · open MakerWorld", 0)
            elif hv["auth"] == "expired":
                await self.panel.status("needed", "cookie expired · open MakerWorld", 0)
            elif hv["stale"]:
                await self.panel.status("stale", "data stale since " + time.strftime("%-I:%M %p", time.localtime(hv["since"])), 0)
            elif age is not None:
                await self.panel.status("ok", f"synced · cookie {_fmt_age(age)}", 0)
            else:
                await self.panel.status("ok", "synced", 0)

            if not cookie:
                await self._push_color(snap, cevents, "needed", "public only - open MakerWorld")
            else:
                await self._push_color(snap, cevents, "ok", "synced")
            self.notifier.submit(prev, snap, self.health_view(), fresh, baseline=not prev.fetched_at or stale)
            self.creator.kick(client, snap)

            result["ok"] = True
            result["snapshot"] = snap.to_dict()
            log.info(
                "poll ok in %.1fs source=%s models=%d downloads=%d prints=%d boosts=%d action=%s color_events=%d (%s)",
                time.time() - started,
                snap.source,
                len(snap.models),
                snap.downloads,
                snap.prints,
                snap.boosts,
                result["action"],
                len(cevents),
                _event_summary(cevents),
            )
            return result

        except AuthError as exc:
            # MakerWorld's front end refuses single requests (403) now and then,
            # and while it is failing (5xx). Only AUTH_REFUSALS refused polls in
            # a row mean an expired cookie; until then the panels keep their state.
            self._auth_fails += 1
            if self._auth_fails < AUTH_REFUSALS:
                store.status.last_poll_ok = False
                store.status.last_error = str(exc)
                store.status.last_poll_at = time.time()
                self.update_health(client, bool(cookie))
                log.warning("poll refused (%s), %d of %d in a row before the cookie counts as expired",
                            exc, self._auth_fails, AUTH_REFUSALS)
                result["error"] = str(exc)
                return result
            store.status.auth_ok = False
            store.status.last_poll_ok = False
            store.status.last_error = str(exc)
            store.status.last_poll_at = time.time()
            log.error(
                "poll auth failed in %.1fs: %s (cookie age %s). Open MakerWorld so the extension can refresh the cookie.",
                time.time() - started,
                exc,
                _fmt_age(age) if age is not None else "none",
            )
            await self.panel.status("needed", "open MakerWorld to refresh cookie", None)
            await self._push_color(prev, [], "needed", "open MakerWorld to refresh cookie")
            self.notifier.submit(None, None, self.health_view())
            result["error"] = str(exc)
            result["auth"] = False
            return result
        except Exception as exc:  # noqa: BLE001
            store.status.last_poll_ok = False
            store.status.last_error = str(exc)
            store.status.last_poll_at = time.time()
            log.error("poll failed in %.1fs: %s", time.time() - started, exc)
            # A failed poll isn't a cookie problem (that is AuthError above).
            # While the data is still fresh the panels keep their last good
            # state (a retry follows in a few minutes); once it goes stale
            # they get a plain message with the real cookie state.
            health = self.health_view()
            if health["stale"]:
                code = re.search(r"\b([45]\d\d)\b", str(exc))
                msg = f"MakerWorld error (HTTP {code.group(1)}), retrying" if code else "MakerWorld not answering, retrying"
                auth = "ok" if self.auth_state == "ok" else "needed"
                await self.panel.status("stale", msg[:40], None)
                await self._push_color(prev, [], auth, msg)
            self.notifier.submit(None, None, health)
            result["error"] = str(exc)
            return result


# Refused polls (401/403) in a row before the cookie counts as expired.
AUTH_REFUSALS = 3

_MODEL_FIELDS = ("downloads", "prints", "boosts", "likes", "collects", "comments", "ratings")
_COLOR_FIELD = {"downloads": "downloads", "prints": "prints", "boosts": "boosts", "likes": "likes",
                "collects": "collects", "comment": "comments", "ratings": "ratings"}
_EINK_FIELD = {"download": "downloads", "print": "prints", "boost": "boosts", "like": "likes",
               "comment": "comments"}


def _fresh_fields(prev: Snapshot, cur: Snapshot) -> set[str]:
    """Per-model stats that were all zero in prev but have data now."""
    if not prev.models or not cur.models:
        return set()
    return {f for f in _MODEL_FIELDS
            if sum(getattr(m, f, 0) for m in prev.models.values()) == 0
            and sum(getattr(m, f, 0) for m in cur.models.values()) > 0}


def _carry_ratings(prev: Snapshot, cur: Snapshot) -> None:
    """Only the design list has ratings. A snapshot from another source has
    none at all; keep the last known ones rather than read that as every
    rating deleted (and then re-added as new on the next full poll)."""
    if any(m.ratings for m in cur.models.values()) or not any(m.ratings for m in prev.models.values()):
        return
    for mid, m in cur.models.items():
        old = prev.models.get(mid)
        if old:
            m.ratings, m.rating_sum = old.ratings, old.rating_sum
    acct = cur.extras.setdefault("account", {})
    acct["ratings"] = sum(m.ratings for m in cur.models.values())
    acct["rating_sum"] = sum(m.rating_sum for m in cur.models.values())


def _totals_changed(a: Snapshot, b: Snapshot) -> bool:
    return (
        a.downloads != b.downloads
        or a.prints != b.prints
        or a.boosts != b.boosts
        or abs(a.points - b.points) > 0.049
        or a.account_name != b.account_name
    )


def _fmt_age(seconds: int) -> str:
    if seconds < 60:
        return f"{seconds}s"
    if seconds < 3600:
        return f"{seconds // 60}m"
    return f"{seconds // 3600}h"


def create_app(settings: Settings, cfg_path=None) -> FastAPI:
    ctx = AppContext(settings)
    app = FastAPI(title="MakerMonitor Poller", version="0.1.0")
    app.state.ctx = ctx

    def require_token(
        authorization: str | None = Header(default=None),
        x_api_token: str | None = Header(default=None, alias="X-Api-Token"),
    ) -> None:
        token = None
        if authorization and authorization.lower().startswith("bearer "):
            token = authorization.split(" ", 1)[1].strip()
        elif x_api_token:
            token = x_api_token.strip()
        if token != settings.api_token:
            raise HTTPException(401, "Unauthorized")

    @app.on_event("startup")
    async def _startup() -> None:
        setup_logging(settings.data_path / "poller.log")
        await ctx.start()
        cookie_age = None
        if ctx.store.status.cookie_updated_at:
            cookie_age = int(time.time() - ctx.store.status.cookie_updated_at)
        log.info(
            "poller up handle=@%s interval=%ss panels=%s cookie=%s age=%s log=%s",
            settings.handle,
            settings.poll_interval_sec,
            ",".join(settings.panel_hosts) or "none",
            "yes" if ctx.store.status.cookie_present else "no",
            _fmt_age(cookie_age) if cookie_age is not None else "none",
            settings.data_path / "poller.log",
        )

    @app.on_event("shutdown")
    async def _shutdown() -> None:
        await ctx.stop()

    @app.get("/health", response_model=None)
    async def health() -> JSONResponse | dict[str, str]:
        hv = ctx.health_view()
        if hv.get("loopStalled"):
            return JSONResponse({"status": "stalled", "loopTick": hv.get("loopTick", 0)}, status_code=503)
        return {"status": "ok"}

    @app.get("/status")
    async def status(_: None = Depends(require_token)) -> dict[str, Any]:
        return {**ctx.store.public_status(), "panels": ctx.panels_view()}

    @app.post("/auth/cookie")
    async def auth_cookie(body: CookieIn, _: None = Depends(require_token)) -> dict[str, Any]:
        header = cookie_header_from(body)
        changed = ctx.store.set_cookie(header)
        log.info(
            "extension cookie from %s changed=%s names=%s bytes=%d",
            body.source,
            changed,
            _cookie_names(header),
            len(header),
        )
        # Kick an early poll when cookie changes (coalesced across bursts)
        if changed:
            ctx.request_poll()
        return {
            "ok": True,
            "changed": changed,
            "cookie_len": len(header),
            "updated_at": ctx.store.status.cookie_updated_at,
        }

    @app.post("/poll/now")
    async def poll_now(_: None = Depends(require_token)) -> dict[str, Any]:
        return await ctx.poll_once()

    # Per-model daily history for the panel's model detail page. Read-only
    # public model counts, fetched lazily by the panel (no token there).
    @app.get("/v2/model")
    async def model_history(id: str = Query(...), full: int = 0) -> dict[str, Any]:  # noqa: A002
        snap = ctx.store.status.last_snapshot
        m = snap.models.get(id) if snap else None
        rows = ctx.day.model_history(id)[-30:]
        cf = ctx.creator_full()
        stats = ctx.creator.model_stats(cf, id) if cf else None
        if stats and not full:
            stats.pop("mwDays", None)  # keeps the panels' 6 KB response buffer enough
        return {"id": id, "name": (m.name if m else "")[:60], "days": rows, "stats": stats}

    # Creator stats in full, for the dashboard and the panels' lazy pages.
    @app.get("/v2/creator")
    async def creator_stats() -> JSONResponse:
        full = ctx.creator_full()
        if not full:
            return JSONResponse({"progress": {"have": 0, "of": 0}, "models": {}, "acct": {}})
        return JSONResponse({**full, "compact": ctx.creator.compact(full, n=10)})

    # LAN dashboard. The api_token opens it once (?token=...), then a cookie.
    def _dash_ok(token: str | None, mm_token: str | None) -> bool:
        return settings.api_token in (token, mm_token)

    @app.get("/", response_class=HTMLResponse)
    @app.get("/dashboard", response_class=HTMLResponse)
    async def dashboard(token: str | None = None, mm_token: str | None = Cookie(default=None)) -> HTMLResponse:
        if not _dash_ok(token, mm_token):
            return HTMLResponse(LOGIN_HTML, status_code=401)
        resp = HTMLResponse(DASHBOARD_HTML)
        if token == settings.api_token:
            resp.set_cookie("mm_token", token, max_age=180 * 86400, httponly=True, samesite="strict")
        return resp

    def _paste_cookie(raw: str) -> bool:
        header = cookie_header_from(CookieIn(cookie=raw, source="dashboard"))
        changed = ctx.store.set_cookie(header)
        log.info("dashboard cookie changed=%s names=%s bytes=%d", changed, _cookie_names(header), len(header))
        if changed:
            ctx.request_poll()
        return changed

    settings_api.register(app, ctx, config_path(cfg_path), _dash_ok, _paste_cookie)

    @app.get("/api/dashboard")
    async def dashboard_data(token: str | None = None, mm_token: str | None = Cookie(default=None)) -> JSONResponse:
        if not _dash_ok(token, mm_token):
            raise HTTPException(401, "Unauthorized")
        st = ctx.last_state
        if st is None:
            snap = ctx.store.status.last_snapshot
            st = build_state(snap, ctx.day, ctx._color_status("ok", ""), ctx.poll_interval) if snap else {}
            st["insights"] = ctx.day.insights()
        else:
            st = dict(st)
        # Live, not from the last push: a stopped poll loop must show here.
        st["health"] = ctx.health_view()
        return JSONResponse(st)

    return app
