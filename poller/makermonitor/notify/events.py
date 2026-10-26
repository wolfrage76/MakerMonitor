"""Notification events: built from a snapshot diff and from poller health."""
from __future__ import annotations

import time
from dataclasses import asdict, dataclass, field
from typing import Any

from ..state import Snapshot

MAKERWORLD = "https://makerworld.com/en"

# Every event type a channel can subscribe to, in the order messages list them.
EVENT_TYPES = ("health", "comment", "follower", "download", "print", "boost", "like", "collect", "rating", "points")

_MODEL_STATS = (("download", "downloads"), ("print", "prints"), ("boost", "boosts"),
                ("like", "likes"), ("collect", "collects"))


@dataclass
class Event:
    type: str
    count: float = 1
    total: float = 0
    model: str = ""
    model_id: str = ""
    user: str = ""
    text: str = ""
    # Ratings only: the stars of the new ratings added up (average = stars / count).
    stars: float = 0
    # Health only: "warn" or "ok", with a short title.
    level: str = ""
    title: str = ""
    t: float = field(default_factory=time.time)

    @property
    def url(self) -> str:
        return f"{MAKERWORLD}/models/{self.model_id}" if self.model_id else ""

    def to_dict(self) -> dict[str, Any]:
        d = {k: v for k, v in asdict(self).items() if v not in ("", None)}
        if self.type == "health":
            d.pop("count"), d.pop("total")
        elif not self.total:
            d.pop("total")
        if not self.stars:
            d.pop("stars")
        if self.url:
            d["url"] = self.url
        return d


def snapshot_events(prev: Snapshot, cur: Snapshot, skip: set[str] | frozenset[str] = frozenset()) -> list[Event]:
    """Activity since the previous snapshot.

    `skip` holds per-model stat names ("downloads", "comments", ...) that are
    being re-baselined and must not be reported as new activity.
    """
    out: list[Event] = []
    if prev.models and cur.models:
        for mid, m in cur.models.items():
            old = prev.models.get(mid)
            if not old:
                continue
            for kind, attr in _MODEL_STATS:
                delta = getattr(m, attr) - getattr(old, attr)
                if delta > 0 and attr not in skip:
                    out.append(Event(kind, delta, getattr(m, attr), m.name or mid, mid))
            delta = m.ratings - old.ratings
            if delta > 0 and "ratings" not in skip:
                out.append(Event("rating", delta, m.ratings, m.name or mid, mid,
                                 stars=max(0, m.rating_sum - old.rating_sum)))
    else:
        for kind, attr in _MODEL_STATS[:3]:
            delta = getattr(cur, attr) - getattr(prev, attr)
            if delta > 0 and attr not in skip:
                out.append(Event(kind, delta, getattr(cur, attr)))

    if "comments" in prev.extras and "comments" not in skip:
        old = prev.extras.get("comments") or []
        seen = {c.get("id") for c in old}
        newest = max((c.get("time") or "" for c in old), default="")
        for c in reversed(cur.extras.get("comments") or []):
            if c.get("id") in seen or (c.get("time") or "") <= newest:
                continue
            mid = str(c.get("designId") or "")
            out.append(Event("comment", 1, 0, c.get("model") or mid, mid,
                             user=c.get("user") or "someone", text=c.get("text") or ""))

    old_f = int((prev.extras.get("account") or {}).get("followers") or 0)
    new_f = int((cur.extras.get("account") or {}).get("followers") or 0)
    if old_f and new_f != old_f:
        out.append(Event("follower", new_f - old_f, new_f))

    if cur.points > prev.points + 0.049 and prev.points:
        out.append(Event("points", round(cur.points - prev.points, 2), round(cur.points, 2)))
    return out


class HealthWatch:
    """Turns the poller's health view into events, on changes only."""

    def __init__(self) -> None:
        self.auth: str | None = None
        self.stale: bool | None = None

    def update(self, health: dict[str, Any] | None) -> list[Event]:
        if not health:
            return []
        out: list[Event] = []
        auth, stale = health.get("auth"), bool(health.get("stale"))
        first = self.auth is None
        if auth != self.auth:
            if auth == "expired":
                out.append(Event("health", level="warn", title="MakerWorld cookie expired",
                                 text="Open MakerWorld in the browser so the extension can refresh it."))
            elif auth == "none" and not first:
                out.append(Event("health", level="warn", title="No MakerWorld cookie",
                                 text="Only public stats until the extension sends a cookie."))
            elif auth == "ok" and self.auth in ("expired", "none"):
                out.append(Event("health", level="ok", title="MakerWorld cookie working again"))
        if stale != self.stale:
            if stale:
                since = int(health.get("since") or 0)
                errs = [f"{k}: {v.get('e')}" for k, v in (health.get("src") or {}).items() if v.get("e")]
                when = time.strftime("%I:%M %p", time.localtime(since)).lstrip("0") if since else "startup"
                text = f"No fresh data since {when}." + (f" Last errors: {'; '.join(errs)}." if errs else "")
                out.append(Event("health", level="warn", title="MakerWorld data is stale", text=text))
            elif not first:
                out.append(Event("health", level="ok", title="MakerWorld data is fresh again"))
        self.auth, self.stale = auth, stale
        return out
