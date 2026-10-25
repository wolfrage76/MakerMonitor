"""Channel base class and registry. A new channel is one subclass with @register."""
from __future__ import annotations

import asyncio
import logging
import os
import re
import time
from typing import Any, ClassVar

import httpx

from .events import EVENT_TYPES, Event
from .style import Digest

log = logging.getLogger(__name__)

CHANNELS: dict[str, type[Channel]] = {}


def register(cls: type[Channel]) -> type[Channel]:
    CHANNELS[cls.kind] = cls
    return cls


_ENV = re.compile(r"\$\{(\w+)\}")


def expand_env(value: Any) -> Any:
    """"${NAME}" in any config string is replaced by that environment variable."""
    if isinstance(value, str):
        def sub(m: re.Match[str]) -> str:
            if m.group(1) not in os.environ:
                raise KeyError(m.group(1))
            return os.environ[m.group(1)]
        return _ENV.sub(sub, value)
    if isinstance(value, dict):
        return {k: expand_env(v) for k, v in value.items()}
    if isinstance(value, list):
        return [expand_env(v) for v in value]
    return value


def parse_quiet(spec: str | None) -> tuple[int, int] | None:
    """"23:00-07:00" -> (minute of day start, end)."""
    if not spec:
        return None
    try:
        a, b = (p.strip() for p in str(spec).split("-"))
        to_min = lambda s: int(s.split(":")[0]) * 60 + int((s.split(":") + ["0"])[1])  # noqa: E731
        return to_min(a), to_min(b)
    except (ValueError, IndexError):
        log.warning("notifications: bad quiet_hours %r (use \"23:00-07:00\")", spec)
        return None


def in_quiet(window: tuple[int, int] | None, now: float | None = None) -> bool:
    if not window:
        return False
    lt = time.localtime(now)
    m = lt.tm_hour * 60 + lt.tm_min
    a, b = window
    return a <= m < b if a <= b else m >= a or m < b


class Channel:
    """One configured destination.

    Subclasses set `kind`, list required config keys in `required`, and
    implement `send(digest)`. Config keys common to every channel:
      events       event types to send (default: all, or the top-level events)
      exclude      event types to leave out of `events`
      quiet_hours  "23:00-07:00": no messages then (except `urgent` types)
      quiet_digest what to do when quiet hours end: true (default) sends one
                   "while you were away" message with everything held, "always"
                   sends it even after a quiet night, false drops what was held
      urgent       types that ignore quiet_hours (default: [health])
      min_count    {type: n}: skip a grouped type until it reaches n per message
      enabled      false turns the channel off without deleting it
    """

    kind: ClassVar[str] = ""
    required: ClassVar[tuple[str, ...]] = ()
    timeout = 15.0

    def __init__(self, cfg: dict[str, Any], defaults: dict[str, Any], handle: str) -> None:
        self.cfg = cfg
        self.handle = handle
        self.name = str(cfg.get("name") or self.kind)
        events = cfg.get("events") or defaults.get("events") or list(EVENT_TYPES)
        if isinstance(events, str):
            events = [e.strip() for e in events.split(",")]
        skip = set(cfg.get("exclude") or defaults.get("exclude") or [])
        unknown = set(events) - set(EVENT_TYPES)
        if unknown:
            log.warning("notifications: %s ignores unknown event types %s", self.name, sorted(unknown))
        self.events = [e for e in events if e in EVENT_TYPES and e not in skip]
        self.quiet = parse_quiet(cfg.get("quiet_hours", defaults.get("quiet_hours")))
        self.urgent = set(cfg.get("urgent", defaults.get("urgent", ["health"])))
        self.min_count = {**(defaults.get("min_count") or {}), **(cfg.get("min_count") or {})}
        self.quiet_digest = cfg.get("quiet_digest", defaults.get("quiet_digest", True))
        self.held: list[Event] = []  # waiting for min_count
        self.quiet_held: list[Event] = []
        self._was_quiet = False
        missing = [k for k in self.required if not cfg.get(k)]
        if missing:
            raise ValueError(f"missing {', '.join(missing)}")

    def accept(self, events: list[Event], now: float | None = None) -> tuple[list[Event], str]:
        """Filter for this channel and apply quiet hours.

        Returns what to send now, and a heading when it is the end-of-quiet digest.
        """
        mine = [e for e in events if e.type in self.events]
        if in_quiet(self.quiet, now):
            self._was_quiet = True
            self.quiet_held += [e for e in mine if e.type not in self.urgent]
            del self.quiet_held[:-500]
            return [e for e in mine if e.type in self.urgent], ""
        heading = ""
        if self._was_quiet:
            self._was_quiet = False
            held, self.quiet_held = self.quiet_held, []
            if self.quiet_digest:
                mine = held + mine
                start = self.quiet[0] if self.quiet else 0
                h = start // 60
                heading = f"While you were away (since {h % 12 or 12}:{start % 60:02d} {'AM' if h < 12 else 'PM'})"
        out, self.held = self.held + mine, []
        for t, n in self.min_count.items():
            if sum(e.count for e in out if e.type == t) < n:
                keep = [e for e in out if e.type == t]
                out = [e for e in out if e.type != t]
                self.held += keep  # carried over until the threshold is reached
        if heading and not out and self.quiet_digest != "always":
            heading = ""
        return out, heading

    async def send(self, digest: Digest) -> None:
        raise NotImplementedError

    async def post(self, url: str, **kw: Any) -> httpx.Response:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            r = await client.post(url, **kw)
            if r.status_code == 429:  # rate limited: wait once as told, then retry
                wait = float(r.headers.get("retry-after") or 2)
                await asyncio.sleep(min(wait, 30))
                r = await client.post(url, **kw)
            r.raise_for_status()
            return r
