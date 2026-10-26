"""Fans one poll's events out to every configured channel without blocking the poll."""
from __future__ import annotations

import asyncio
import logging
from typing import Any

import httpx

from ..state import Snapshot
from . import (  # noqa: F401 - registers the built-in channels
    apprise_channel, discord, homeassistant, ntfy, pushbullet, pushover, telegram, webhook,
)
from .base import CHANNELS, Channel, expand_env
from .events import Event, HealthWatch, snapshot_events
from .style import build_digest

log = logging.getLogger(__name__)


class NotifyHub:
    def __init__(self, cfg: dict[str, Any] | None, handle: str) -> None:
        cfg = cfg or {}
        self.handle = handle
        self.health = HealthWatch()
        self.channels: list[Channel] = []
        self._tasks: set[asyncio.Task] = set()
        defaults = {k: v for k, v in cfg.items() if k != "channels"}
        for i, raw in enumerate(cfg.get("channels") or []):
            label = (raw or {}).get("name") or (raw or {}).get("type") or f"#{i + 1}"
            if not isinstance(raw, dict) or raw.get("enabled") is False:
                continue
            cls = CHANNELS.get(str(raw.get("type", "")).lower())
            if cls is None:
                log.warning("notifications: %s has unknown type %r (have: %s)", label, raw.get("type"), ", ".join(CHANNELS))
                continue
            try:
                self.channels.append(cls(expand_env(raw), defaults, handle))
            except KeyError as exc:
                log.warning("notifications: %s disabled, environment variable %s is not set", label, exc)
            except ValueError as exc:
                log.warning("notifications: %s disabled, %s", label, exc)
        self._announced = False

    def submit(self, prev: Snapshot | None, cur: Snapshot | None, health: dict[str, Any] | None = None,
               skip: set[str] | frozenset[str] = frozenset(), baseline: bool = False) -> list[Event]:
        """Called once per poll. `baseline` means don't report activity (first poll, long gap)."""
        if not self.channels:
            return []
        if not self._announced:  # logged here because the log file isn't set up yet in __init__
            self._announced = True
            log.info("notifications: %s", ", ".join(f"{c.name} ({c.kind})" for c in self.channels))
        events = self.health.update(health)
        if prev is None and cur is None and not events:
            return []  # failed poll with nothing new: keep quiet-hours state for the next good one
        if prev is not None and cur is not None and not baseline:
            try:
                events += snapshot_events(prev, cur, skip)
            except Exception as exc:  # noqa: BLE001 - never break the poll
                log.exception("notifications: event diff failed: %s", exc)
        self.dispatch(events)
        return events

    def dispatch(self, events: list[Event]) -> None:
        for ch in self.channels:
            batch, heading = ch.accept(events)
            if batch or heading:
                task = asyncio.create_task(self._send(ch, batch, heading))
                self._tasks.add(task)
                task.add_done_callback(self._tasks.discard)

    async def _send(self, ch: Channel, events: list[Event], heading: str = "") -> bool:
        digest = build_digest(events, self.handle, heading)
        try:
            await ch.send(digest)
            log.info("notify %s: %s", ch.name, digest.summary)
            return True
        except Exception as exc:  # noqa: BLE001
            log.warning("notify %s failed: %s (%s)", ch.name, _clean(exc), digest.summary)
            return False

    async def test(self) -> dict[str, bool]:
        """Send a sample message to every channel, ignoring filters and quiet hours."""
        sample = [
            Event("health", level="ok", title="MakerMonitor test message",
                  text="Notifications are set up. This is how each kind of event looks."),
            Event("comment", 1, model="Sample Model", model_id="123456", user="A Maker", text="Printed great, thanks!"),
            Event("follower", 1, 321),
            Event("download", 3, 1205, "Sample Model", "123456"),
            Event("download", 1, 88, "Another Model", "654321"),
            Event("like", 2, 140, "Sample Model", "123456"),
            Event("rating", 1, 34, "Sample Model", "123456", stars=5),
        ]
        out = {}
        for ch in self.channels:
            out[ch.name] = await self._send(ch, [e for e in sample if e.type in ch.events] or sample)
        return out


def _clean(exc: Exception) -> str:
    """Error text without the request URL (Discord webhook URLs are secrets)."""
    if isinstance(exc, httpx.HTTPStatusError):
        return f"HTTP {exc.response.status_code} {exc.response.text[:120]}"
    return f"{type(exc).__name__}: {exc}" if str(exc) else type(exc).__name__
