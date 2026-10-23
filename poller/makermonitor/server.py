from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from fastapi import Depends, FastAPI, Header, HTTPException
from pydantic import BaseModel

from .color import ColorPanelClient, build_state, color_events
from .config import Settings
from .daily import DayTracker
from .makerworld import AuthError, MakerWorldClient
from .pages import build_sections
from .panel import PanelClient, build_events
from .state import Snapshot, Store

log = logging.getLogger(__name__)


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
        while True:
            if time.time() - last >= self.poll_interval:
                last = time.time()
                try:
                    await self.poll_once()
                except Exception as exc:  # noqa: BLE001
                    log.exception("Poll loop error: %s", exc)
            # Wake every 15 s to pick up panel settings / "poll now".
            await asyncio.sleep(max(1.0, min(15.0, last + self.poll_interval - time.time())))
            if await self._check_color_settings():
                last = 0.0

    async def _check_color_settings(self) -> bool:
        """Returns True when a panel (e-ink or color) asked for an immediate poll.

        The poll interval follows whichever panel's setting changed last.
        """
        poll_now = False
        for panel in [self.panel, *self.colors]:
            cfg = await panel.settings()
            if not cfg:
                continue
            sec = int(cfg.get("pollSec") or 0)
            seq = cfg.get("pollNow")
            seen = self._color_seen.get(panel.base_url)
            if seen is None:
                log.info("Panel %s (%s): pollSec=%s pollNow=%s", panel.base_url, cfg.get("board", "?"), sec, seq)
            if sec and 60 <= sec <= 3600 and (seen is None or seen[0] != sec) and sec != self.poll_interval:
                log.info("Poll interval %ss -> %ss (%s)", self.poll_interval, sec, panel.base_url)
                self.poll_interval = sec
            if seen is not None and seq != seen[1]:
                poll_now = True
            self._color_seen[panel.base_url] = (sec, seq)
        return poll_now

    def _color_status(self, auth: str, message: str) -> dict[str, Any]:
        st = self.store.status
        age = int(time.time() - st.cookie_updated_at) if st.cookie_updated_at else -1
        return {"auth": auth, "msg": message, "cookieAge": age, "lastPoll": int(st.last_poll_at),
                "ok": st.last_poll_ok}

    async def _push_color(self, snap: Snapshot, events: list[dict[str, Any]], auth: str, message: str) -> None:
        if not self.colors:
            return
        try:
            state = build_state(snap, self.day, self._color_status(auth, message), self.poll_interval)
        except Exception as exc:  # noqa: BLE001 - never break the e-ink path
            log.exception("Color state build failed: %s", exc)
            return
        for panel in self.colors:
            try:
                await panel.state(state)
                for ev in events:
                    await panel.event(ev)
                    await asyncio.sleep(0.2)
            except Exception as exc:  # noqa: BLE001 - never break the e-ink path
                log.exception("Color panel push failed (%s): %s", panel.base_url, exc)

    async def poll_once(self) -> dict[str, Any]:
        async with self._poll_lock:
            return await self._poll_once_unlocked()

    async def _poll_once_unlocked(self) -> dict[str, Any]:
        settings = self.settings
        store = self.store
        cookie = store.get_cookie()
        client = MakerWorldClient(settings.handle, settings.user_agent, cookie)
        prev = store.status.last_snapshot
        result: dict[str, Any] = {"ok": False}

        store.status.panel_reachable = await self.panel.ping()

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
                await self.panel.sync(snap)
                result["action"] = "initial_sync"
            else:
                events = [e for e in build_events(prev, snap) if _EINK_FIELD.get(e.get("type")) not in fresh]
                if events:
                    for ev in events:
                        await self.panel.notify(ev)
                        await asyncio.sleep(0.3)
                    result["action"] = f"notified:{len(events)}"
                elif _totals_changed(prev, snap):
                    await self.panel.sync(snap)
                    result["action"] = "sync_totals"
                else:
                    result["action"] = "unchanged"

            # Sent every poll so a rebooted panel gets its sections back.
            await self.panel.pages(build_sections(snap, settings.page_cycle_sec, self.day.view()))

            age = int(time.time() - store.status.cookie_updated_at) if store.status.cookie_updated_at else None
            if not cookie:
                await self.panel.status("needed", "public only · open MakerWorld", 0)
            elif age is not None:
                await self.panel.status("ok", f"synced · cookie {_fmt_age(age)}", 0)
            else:
                await self.panel.status("ok", "synced", 0)

            if not cookie:
                await self._push_color(snap, cevents, "needed", "public only - open MakerWorld")
            else:
                await self._push_color(snap, cevents, "ok", "synced")

            result["ok"] = True
            result["snapshot"] = snap.to_dict()
            return result

        except AuthError as exc:
            store.status.auth_ok = False
            store.status.last_poll_ok = False
            store.status.last_error = str(exc)
            store.status.last_poll_at = time.time()
            await self.panel.status("needed", "open MakerWorld to refresh cookie", None)
            await self._push_color(prev, [], "needed", "open MakerWorld to refresh cookie")
            result["error"] = str(exc)
            result["auth"] = False
            return result
        except Exception as exc:  # noqa: BLE001
            store.status.last_poll_ok = False
            store.status.last_error = str(exc)
            store.status.last_poll_at = time.time()
            await self.panel.status("error", str(exc)[:40], None)
            await self._push_color(prev, [], "error", str(exc)[:60])
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


def create_app(settings: Settings) -> FastAPI:
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
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s %(levelname)s %(name)s: %(message)s",
        )
        await ctx.start()
        log.info(
            "Poller listening; handle=@%s panel=%s interval=%ss",
            settings.handle,
            settings.panel_base,
            settings.poll_interval_sec,
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

    return app
