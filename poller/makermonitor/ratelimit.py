"""One limiter for all MakerWorld traffic from this poller.

At most one request in flight, a minimum gap between requests (plus
jitter) and an hourly budget. Core poll requests (the regular stats poll)
only wait for the gap; background work (per-model stats, backfill,
comment scans) also needs leftover budget, waits while a core poll is
running and pauses while any endpoint is backing off.

Per endpoint: on 429, 403 or 5xx the endpoint backs off exponentially
(1 min, 2, 4 ... up to 1 h, with jitter), or for Retry-After when that is
longer. A success resets it. Background work pauses entirely after any
429, or while the core profile / design list is backing off; otherwise
only the failing background endpoint pauses. (The account-level 30-day
report is refused now and then on its own, so it doesn't stop the rest.)
"""

from __future__ import annotations

import asyncio
import json
import logging
import random
import time
from collections import deque
from pathlib import Path
from typing import Any

from .state import atomic_write_text

log = logging.getLogger(__name__)

MIN_GAP = 3.0          # seconds between requests
JITTER = 1.5           # extra random 0..JITTER seconds
HOURLY_BUDGET = 120    # all MakerWorld requests per rolling hour
CORE_RESERVE = 40      # part of the budget background work leaves for core polls
BACKOFF_START = 60.0
BACKOFF_MAX = 3600.0
_RL_HEADERS = ("retry-after", "x-ratelimit-limit", "x-ratelimit-remaining", "x-ratelimit-reset",
               "ratelimit-limit", "ratelimit-remaining", "ratelimit-reset")


class RateLimited(Exception):
    """The endpoint is backing off or the budget is used up; not a MakerWorld error."""


class Limiter:
    def __init__(self) -> None:
        self._lock = asyncio.Lock()
        self._last = 0.0
        self._sent: deque[tuple[float, str]] = deque()
        self.core_active = 0
        # endpoint -> {"until": t, "level": n, "status": code}
        self.backoff: dict[str, dict[str, Any]] = {}
        self.seen_headers: dict[str, str] = {}
        self._hour_logged = 0
        self._path: Path | None = None

    def load(self, data_dir: Path) -> None:
        """Keep the last hour's requests and any backoff across restarts."""
        self._path = data_dir / "ratelimit.json"
        try:
            d = json.loads(self._path.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            return
        now = time.time()
        self._sent = deque((t, k) for t, k in d.get("sent", []) if now - t < 3600)
        self.backoff = {k: b for k, b in (d.get("backoff") or {}).items() if b.get("until", 0) > now}
        if self._sent:
            self._last = self._sent[-1][0]

    def _save(self) -> None:
        if not self._path:
            return
        try:
            atomic_write_text(self._path, json.dumps({"sent": list(self._sent), "backoff": self.backoff}))
        except OSError:
            pass

    # ---- budget ------------------------------------------------------------

    def _trim(self, now: float) -> None:
        while self._sent and now - self._sent[0][0] > 3600:
            self._sent.popleft()

    def used_last_hour(self, now: float | None = None) -> int:
        self._trim(now or time.time())
        return len(self._sent)

    def by_kind(self) -> dict[str, int]:
        self._trim(time.time())
        out: dict[str, int] = {}
        for _, k in self._sent:
            out[k] = out.get(k, 0) + 1
        return out

    def backing_off(self, endpoint: str | None = None) -> bool:
        now = time.time()
        if endpoint is not None:
            return self.backoff.get(endpoint, {}).get("until", 0) > now
        return any(b.get("until", 0) > now for b in self.backoff.values())

    def paused(self) -> bool:
        """Background work stops after a 429 anywhere or a core profile/list failure."""
        now = time.time()
        return any(b["until"] > now and (b.get("status") == 429 or k in ("profile", "models"))
                   for k, b in self.backoff.items())

    def background_ok(self, endpoint: str = "") -> bool:
        """Room for one more low-priority request right now."""
        return (not self.core_active and not self.paused() and not self.backing_off(endpoint)
                and self.used_last_hour() < HOURLY_BUDGET - CORE_RESERVE)

    # ---- requests ----------------------------------------------------------

    async def get(self, client: Any, url: str, *, endpoint: str, core: bool, **kw: Any) -> Any:
        """client.get(url) through the limiter. Raises RateLimited instead of sending."""
        if not core:
            if self.paused():
                raise RateLimited("paused")
            if self.used_last_hour() >= HOURLY_BUDGET - CORE_RESERVE:
                raise RateLimited("hourly budget used")
        b = self.backoff.get(endpoint)
        if b and b["until"] > time.time() and (not core or b.get("status") == 429):
            raise RateLimited(f"{endpoint} backing off until {time.strftime('%-I:%M %p', time.localtime(b['until']))}")
        async with self._lock:
            if not core and (self.core_active or self.paused()):
                raise RateLimited("core poll running" if self.core_active else "paused")
            wait = self._last + MIN_GAP + random.uniform(0, JITTER) - time.time()
            if wait > 0:
                await asyncio.sleep(wait)
            now = time.time()
            self._sent.append((now, "core" if core else "bg"))
            self._log_hour(now)
            try:
                r = await client.get(url, **kw)
            finally:
                self._last = time.time()
        self._note(endpoint, r)
        return r

    def _note(self, endpoint: str, r: Any) -> None:
        hdrs = getattr(r, "headers", {}) or {}
        for h in _RL_HEADERS:
            v = hdrs.get(h)
            if v is not None and self.seen_headers.get(h) != v:
                self.seen_headers[h] = str(v)
                log.info("MakerWorld %s header %s: %s", endpoint, h, v)
        code = int(getattr(r, "status_code", 0) or 0)
        if code == 429 or code == 403 or code >= 500:
            prev = self.backoff.get(endpoint) or {"level": 0}
            level = prev["level"] + 1
            delay = min(BACKOFF_MAX, BACKOFF_START * 2 ** (level - 1)) * random.uniform(0.8, 1.2)
            ra = _retry_after(hdrs.get("retry-after"))
            if ra:
                delay = max(delay, min(ra, 6 * 3600))
            self.backoff[endpoint] = {"until": time.time() + delay, "level": level, "status": code}
            log.warning("MakerWorld %s HTTP %d; backing off %.0fs (level %d)", endpoint, code, delay, level)
        elif 200 <= code < 400 and endpoint in self.backoff:
            del self.backoff[endpoint]
        self._save()

    def _log_hour(self, now: float) -> None:
        hour = int(now // 3600)
        if hour != self._hour_logged:
            if self._hour_logged:
                log.info("MakerWorld requests last hour: %d (%s)", self.used_last_hour(now), self.by_kind())
            self._hour_logged = hour

    def view(self) -> dict[str, Any]:
        now = time.time()
        return {
            "requests_last_hour": self.used_last_hour(now),
            "by_kind": self.by_kind(),
            "budget": HOURLY_BUDGET,
            "backoff_until": {k: int(b["until"]) for k, b in self.backoff.items() if b["until"] > now},
            "paused": self.paused(),
            "headers": dict(self.seen_headers),
        }


def _retry_after(v: Any) -> float:
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0


LIMITER = Limiter()
