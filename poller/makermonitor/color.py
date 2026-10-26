"""Payloads for the color (ESP32-S3 3.16") panel.

The panel gets one full state per poll (POST /v2/state) and one event per
change (POST /v2/event). It also serves GET /v2/settings, which the poller
reads to pick up the poll interval and "poll now" presses from the knob.
"""

from __future__ import annotations

import logging
import time
from datetime import date
from typing import Any

import httpx

from .daily import STATS, DayTracker
from .pages import ascii_text
from .panel import build_events
from .state import Snapshot

log = logging.getLogger(__name__)

# e-ink event type -> color panel kind
_KIND = {"boost": "boosts", "like": "likes", "download": "downloads", "print": "prints",
         "comment": "comment"}


def color_events(prev: Snapshot, cur: Snapshot) -> list[dict[str, Any]]:
    """Per-model events for the five stats, ratings and comments."""
    out: list[dict[str, Any]] = []
    for e in build_events(prev, cur):
        kind = _KIND.get(e.get("type", ""))
        if not kind:
            continue
        ev = {"k": kind, "n": int(e.get("value", 1)), "m": ascii_text(e.get("modelName", ""))[:60]}
        if kind == "comment":
            ev["u"] = ascii_text(e.get("user", ""))[:32]
            ev["x"] = ascii_text(e.get("text", ""))[:120]
        out.append(ev)
    # build_events has no collects; diff them here.
    if prev.models and cur.models:
        for mid, m in cur.models.items():
            old = prev.models.get(mid)
            if old and m.collects > old.collects:
                out.append({"k": "collects", "n": m.collects - old.collects,
                            "m": ascii_text(m.name or mid)[:60]})
            if old and m.ratings > old.ratings:
                n = m.ratings - old.ratings
                # s: average stars of the new ratings (exact when n == 1)
                out.append({"k": "ratings", "n": n, "m": ascii_text(m.name or mid)[:60],
                            "s": round(max(0, m.rating_sum - old.rating_sum) / n, 1)})
    return out


def _top(snap: Snapshot, base: dict[str, dict[str, int]] | None, count: int = 4) -> list[dict[str, Any]]:
    rows = []
    for mid, m in snap.models.items():
        cur = {"boosts": m.boosts, "likes": m.likes, "collects": m.collects,
               "downloads": m.downloads, "prints": m.prints}
        if base is not None:
            b = base.get(mid, {})
            vals = {k: cur[k] - int(b.get(k, 0)) for k in STATS}
        else:
            vals = cur
        # Weight by how much each action is worth to a creator.
        score = vals["prints"] * 3 + vals["downloads"] * 2 + vals["boosts"] * 5 + vals["collects"] + vals["likes"]
        if score <= 0:
            continue
        rows.append((score, {"id": str(mid), "n": ascii_text(m.name or mid)[:60], "b": vals["boosts"], "l": vals["likes"],
                             "c": vals["collects"], "d": vals["downloads"], "p": vals["prints"], "s": score}))
    rows.sort(key=lambda r: -r[0])
    return [r[1] for r in rows[:count]]


def followers_view(total: int, today: int, daily: list[int]) -> dict[str, Any]:
    """Follower count and gains. series30: gains per day, oldest first, ending
    with today's live gain (MakerWorld's daily list stops at yesterday)."""
    series = [int(x) for x in daily[-29:]] + [today]
    return {"total": total, "today": today, "d7": sum(series[-7:]), "d30": sum(series),
            "days": len(series), "series30": series}


def build_state(snap: Snapshot, day: DayTracker, status: dict[str, Any], poll_sec: int) -> dict[str, Any]:
    v = day.view()
    acct = snap.extras.get("account") or {}
    bases = v["model_bases"]
    week_key = sorted(bases)[0] if bases else None
    month = dict(snap.extras.get("month") or {})
    yday_reach = month.pop("yesterday", {}) or {}
    fol_days = month.pop("followers_daily", None) or []
    month["yday_impressions"] = yday_reach.get("impressions", 0)
    month["yday_views"] = yday_reach.get("views", 0)
    ratings = sum(m.ratings for m in snap.models.values())
    rating_sum = sum(m.rating_sum for m in snap.models.values())
    comments = [
        {"u": ascii_text(c.get("user", ""))[:32], "x": ascii_text(c.get("text", ""))[:160],
         "m": ascii_text(c.get("model", ""))[:60], "t": c.get("time", "")}
        for c in (snap.extras.get("comments") or [])[:9]
    ]
    return {
        "v": 1,
        "t": int(time.time()),
        # Poller host's UTC offset, so the panel shows local times without a TZ setting.
        "tz": time.localtime().tm_gmtoff,
        "account": ascii_text(snap.account_name),
        "today": v["today"],
        "since": int(v["since"]),
        "midnight": v["midnight"],
        "yday": v["yday"],
        "total": {
            "boosts": snap.boosts, "likes": int(acct.get("likes", 0)), "collects": int(acct.get("saves", 0)),
            "downloads": snap.downloads, "prints": snap.prints, "points": round(snap.points, 2),
            "followers": int(acct.get("followers", 0)), "comments": int(acct.get("comments", 0)),
            "models": int(acct.get("models", len(snap.models))),
            "ratings": ratings, "ratingAvg": round(rating_sum / ratings, 2) if ratings else 0,
        },
        "hours": v["hours"],
        "days": [{**{k: d.get(k, 0) for k in (*STATS, "points")},
                  "d": date.fromisoformat(d["date"]).strftime("%a")} for d in v["days"]],
        "top": {
            "today": _top(snap, v["base_models"]),
            "week": _top(snap, bases.get(week_key) if week_key else None),
            "life": _top(snap, None),
        },
        "feed": [{k: e[k] for k in ("k", "n", "m", "t", "s") if k in e} for e in v["feed"]],
        "month": month,
        "followers": followers_view(int(acct.get("followers", 0)), int(v["today"].get("followers", 0)), fol_days),
        "comments": comments,
        "status": {**status, "pollSec": poll_sec},
    }


class ColorPanelClient:
    def __init__(self, base_url: str, timeout: float = 8.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    async def settings(self) -> dict[str, Any] | None:
        try:
            async with httpx.AsyncClient(timeout=httpx.Timeout(3.5, connect=1.5)) as client:
                r = await client.get(f"{self.base_url}/v2/settings")
                if r.status_code == 200:
                    return r.json()
        except (httpx.HTTPError, ValueError):
            pass
        return None

    async def state(self, body: dict[str, Any]) -> bool:
        return await self._post("/v2/state", body)

    async def event(self, body: dict[str, Any]) -> bool:
        return await self._post("/v2/event", body)

    async def _post(self, path: str, body: dict[str, Any]) -> bool:
        try:
            async with httpx.AsyncClient(timeout=httpx.Timeout(self.timeout, connect=2.0)) as client:
                r = await client.post(f"{self.base_url}{path}", json=body)
                if r.status_code >= 400:
                    log.warning("Color panel %s -> %s", path, r.status_code)
                    return False
                return True
        except httpx.HTTPError as exc:
            log.warning("Color panel %s failed: %s", path, exc)
            return False
