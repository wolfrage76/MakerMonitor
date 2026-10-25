from __future__ import annotations

import asyncio
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
from .panel import PanelClient, build_events
from .ratelimit import LIMITER
from . import settings_api
from .state import Snapshot, Store

log = logging.getLogger(__name__)

# Seconds between retries when polls fail right after startup.
STARTUP_RETRIES = (15, 30, 60, 120)


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
        self.panel = PanelClient(settings.panel_base)
        self.colors = [ColorPanelClient(base) for base in settings.color_panel_bases]
        self.day = DayTracker(settings.data_path)
        # Any panel's Settings menu (e-ink or color) can override the interval.
        self.poll_interval = settings.poll_interval_sec
        self._color_seen: dict[str, tuple[int, Any]] = {}  # base -> (pollSec, pollNow seq)
        self._poll_lock = asyncio.Lock()
        self._task: asyncio.Task | None = None
        # Per-source fetch health for the panels and the dashboard.
        self.health: dict[str, dict[str, Any]] = {k: {"t": 0.0, "fails": 0, "err": ""}
                                                  for k in ("models", "reach", "points")}
        # Creator stats (background, low priority); its health is its own source.
        LIMITER.load(settings.data_path)
        self.creator = CreatorStats(settings.data_path)
        self.health["model-stats"] = self.creator.health
        self.auth_state = "none"
        self._health_client: Any = None
        self._self_url = ""
        self.last_state: dict[str, Any] | None = None
        self.notifier = NotifyHub(settings.notifications, settings.handle)

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
        # Points and models only answer with a valid cookie; data-overview
        # (reach) is sometimes refused on its own, so it doesn't count here.
        elif any(client.results.get(k) == "auth" for k in ("models", "points")):
            self.auth_state = "expired"
        elif client.results.get("models") == "ok":
            self.auth_state = "ok"

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
        if self.panel.base_url != old.panel_base:
            self.panel = PanelClient(old.panel_base)
        have = {p.base_url: p for p in self.colors}
        self.colors = [have.get(b) or ColorPanelClient(b) for b in old.color_panel_bases]
        live = {old.panel_base, *old.color_panel_bases}
        self._color_seen = {k: v for k, v in self._color_seen.items() if k in live}
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
        log.info("settings applied: interval=%ss eink=%s color=%s notify=%s", self.poll_interval,
                 old.panel_base or "none", ",".join(old.color_panel_bases) or "none",
                 ",".join(c.name for c in hub.channels) or "none")

    def health_view(self) -> dict[str, Any]:
        m = self.health["models"]
        stale = bool(m["t"]) and time.time() - m["t"] > 3 * self.poll_interval or (not m["t"] and m["fails"] > 0)
        return {
            "auth": self.auth_state,
            "stale": stale,
            "since": int(m["t"]),
            "src": {k: {"t": int(h["t"]), "f": h["fails"], "e": h["err"][:40]} for k, h in self.health.items()},
            # All MakerWorld requests from this poller (ratelimit.py).
            "limiter": LIMITER.view(),
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
            hosts = [b.split("//", 1)[-1].split(":")[0] for b in self.settings.color_panel_bases]
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sk:
                    sk.connect((socket.gethostbyname(hosts[0]) if hosts else "192.168.1.1", 80))
                    ip = sk.getsockname()[0]
            except OSError:
                pass
            self._self_url = f"http://{ip}:{self.settings.listen_port}"
        return self._self_url

    async def start(self) -> None:
        self._task = asyncio.create_task(self._poll_loop(), name="poll-loop")

    async def stop(self) -> None:
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def _poll_loop(self) -> None:
        # Small delay so HTTP server is up first
        await asyncio.sleep(2)
        last = 0.0
        # Right after boot the network or cookie is often not ready yet, so
        # failed polls retry on a short schedule until one succeeds. Requests
        # still go through LIMITER, so its hourly cap and backoff apply.
        retries: list[int] | None = list(STARTUP_RETRIES)
        wait: int | None = None
        while True:
            if time.time() - last >= (wait or self.poll_interval):
                last = time.time()
                wait = None
                ok = False
                try:
                    ok = bool((await self.poll_once()).get("ok"))
                except Exception as exc:  # noqa: BLE001
                    log.exception("Poll loop error: %s", exc)
                if ok:
                    retries = None
                elif retries:
                    wait = retries.pop(0)
                    log.info("startup poll failed; retrying in %ss (%d retries left)", wait, len(retries))
                elif retries is not None:
                    retries = None
                    log.info("startup retries used up; back to the %ss interval", self.poll_interval)
            # Wake every 15 s to pick up panel settings / "poll now".
            await asyncio.sleep(max(1.0, min(15.0, last + (wait or self.poll_interval) - time.time())))
            if await self._check_color_settings():
                last = 0.0

    async def _check_color_settings(self) -> bool:
        """Returns True when a panel (e-ink or color) asked for an immediate poll.

        The poll interval follows whichever panel's setting changed last.
        """
        poll_now = False
        for panel in [p for p in (self.panel, *self.colors) if p.base_url]:
            cfg = await panel.settings()
            if not cfg:
                down = self._color_seen.get(panel.base_url)
                if down != ("down", None):
                    log.warning("panel %s settings unreachable", panel.base_url)
                    self._color_seen[panel.base_url] = ("down", None)
                continue
            sec = int(cfg.get("pollSec") or 0)
            seq = cfg.get("pollNow")
            seen = self._color_seen.get(panel.base_url)
            if seen is None:
                log.info("Panel %s (%s): pollSec=%s pollNow=%s", panel.base_url, cfg.get("board", "?"), sec, seq)
            if sec and 60 <= sec <= 3600 and (seen is None or seen[0] != sec) and sec != self.poll_interval:
                log.info("Poll interval %ss -> %ss (%s)", self.poll_interval, sec, panel.base_url)
                self.poll_interval = sec
            if seen not in (None, ("down", None)) and seq != seen[1]:
                poll_now = True
            self._color_seen[panel.base_url] = (sec, seq)
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
        for panel in self.colors:
            try:
                ok = await panel.state(state)
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

        store.status.panel_reachable = await self.panel.ping()
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
            store.status.auth_ok = bool(cookie)
            store.status.last_poll_ok = True
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
            await self.panel.status("error", str(exc)[:40], None)
            await self._push_color(prev, [], "error", str(exc)[:60])
            self.notifier.submit(None, None, self.health_view())
            result["error"] = str(exc)
            return result


_MODEL_FIELDS = ("downloads", "prints", "boosts", "likes", "collects", "comments")
_COLOR_FIELD = {"downloads": "downloads", "prints": "prints", "boosts": "boosts", "likes": "likes",
                "collects": "collects", "comment": "comments"}
_EINK_FIELD = {"download": "downloads", "print": "prints", "boost": "boosts", "like": "likes",
               "comment": "comments"}


def _fresh_fields(prev: Snapshot, cur: Snapshot) -> set[str]:
    """Per-model stats that were all zero in prev but have data now."""
    if not prev.models or not cur.models:
        return set()
    return {f for f in _MODEL_FIELDS
            if sum(getattr(m, f, 0) for m in prev.models.values()) == 0
            and sum(getattr(m, f, 0) for m in cur.models.values()) > 0}


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
            "poller up handle=@%s interval=%ss eink=%s color=%s cookie=%s age=%s log=%s",
            settings.handle,
            settings.poll_interval_sec,
            settings.panel_base or "none",
            ",".join(settings.color_panel_bases) or "none",
            "yes" if ctx.store.status.cookie_present else "no",
            _fmt_age(cookie_age) if cookie_age is not None else "none",
            settings.data_path / "poller.log",
        )

    @app.on_event("shutdown")
    async def _shutdown() -> None:
        await ctx.stop()

    @app.get("/health")
    async def health() -> dict[str, str]:
        return {"status": "ok"}

    @app.get("/status")
    async def status(_: None = Depends(require_token)) -> dict[str, Any]:
        return ctx.store.public_status()

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
        # Kick an early poll when cookie changes
        if changed:
            asyncio.create_task(ctx.poll_once())
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
            asyncio.create_task(ctx.poll_once())
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
            st["health"] = ctx.health_view()
            st["insights"] = ctx.day.insights()
        return JSONResponse(st)

    return app
