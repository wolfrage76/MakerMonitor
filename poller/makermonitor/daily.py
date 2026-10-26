"""Day-level counters for the color panel.

MakerWorld only exposes lifetime totals, so "today" is lifetime minus a
baseline taken at the first poll of each local day (the last poll before
midnight, when there is one). Keeps yesterday, 30 days of daily totals,
per-hour activity for today, per-model baselines (for top-model ranking)
and a short event feed. Persisted to data/daily.json.
"""

from __future__ import annotations

import json
import threading
import time
from datetime import date, datetime
from pathlib import Path
from typing import Any

from .state import Snapshot

STATS = ("boosts", "likes", "collects", "downloads", "prints")
MODEL_KEYS = {"boosts": "boosts", "likes": "likes", "collects": "collects",
              "downloads": "downloads", "prints": "prints"}
KEEP_DAYS = 30
KEEP_MODEL_BASES = 31   # per-model daily history for the panel's model detail
KEEP_HOUR_DAYS = 35     # per-day hourly buckets for best-time insights
KEEP_FEED = 40
# Counters that joined after the first release. A baseline without one counts
# from the first poll that has it, so the lifetime total never shows as today.
LATE = ("followers", "ratings", "rating_sum")


def counters(snap: Snapshot) -> dict[str, float]:
    acct = snap.extras.get("account") or {}
    return {
        "boosts": snap.boosts,
        "likes": int(acct.get("likes", 0)),
        "collects": int(acct.get("saves", 0)),
        "downloads": snap.downloads,
        "prints": snap.prints,
        "points": round(snap.points, 2),
        "followers": int(acct.get("followers", 0)),
        "ratings": sum(m.ratings for m in snap.models.values()),
        "rating_sum": sum(m.rating_sum for m in snap.models.values()),
    }


def model_counts(snap: Snapshot) -> dict[str, dict[str, int]]:
    return {
        mid: {k: int(getattr(m, attr)) for k, attr in MODEL_KEYS.items()}
        for mid, m in snap.models.items()
    }


def _diff(a: dict[str, float], b: dict[str, float]) -> dict[str, float]:
    out = {k: b.get(k, 0) - a.get(k, 0) for k in (*STATS, "points")}
    out["points"] = round(out["points"], 2)
    for k in LATE:
        out[k] = int(b[k] - a[k]) if k in a and k in b else 0
    return out


class DayTracker:
    def __init__(self, data_dir: Path) -> None:
        self.path = data_dir / "daily.json"
        self._lock = threading.RLock()
        try:
            self.d: dict[str, Any] = json.loads(self.path.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            self.d = {}

    def _save(self) -> None:
        self.path.write_text(json.dumps(self.d))

    def update(self, snap: Snapshot, now: float | None = None) -> None:
        """Fold one successful poll into the day state."""
        now = now or snap.fetched_at or time.time()
        today = date.fromtimestamp(now).isoformat()
        cur = counters(snap)
        models = model_counts(snap)
        with self._lock:
            d = self.d
            if d.get("day") != today:
                self._roll(d, today, now, cur, models)
            # Baselines from before a counter was tracked: count from now.
            for b in (d.get("base"), d.get("last")):
                for k in LATE:
                    if b is not None and k not in b:
                        b[k] = cur[k]
            last = d.get("last") or cur
            delta = _diff(last, cur)
            hour = datetime.fromtimestamp(now).hour
            hours = d.setdefault("hours", {k: [0] * 24 for k in STATS})
            for k in STATS:
                # A stat going from 0 to its lifetime total means the poller just
                # started reading it (e.g. per-model likes/collects), not activity.
                if delta[k] > 0 and last.get(k, 0) > 0:
                    hours[k][hour] += int(delta[k])
            d["last"] = cur
            d["last_t"] = now
            d["last_models"] = models
            self._save()

    def _roll(self, d: dict[str, Any], today: str, now: float,
              cur: dict[str, float], models: dict[str, dict[str, int]]) -> None:
        last = d.get("last")
        if d.get("day") and d.get("base") and last:
            # Close out the previous day using the last poll we saw for it.
            closed = _diff(d["base"], last)
            closed["date"] = d["day"]
            days = [x for x in d.get("days", []) if x.get("date") != d["day"]]
            days.append(closed)
            d["days"] = days[-KEEP_DAYS:]
            d["yday"] = closed if date.fromisoformat(d["day"]).toordinal() == date.fromisoformat(today).toordinal() - 1 else None
            # Last pre-midnight poll is the best baseline we have.
            d["base"], d["base_t"] = last, d.get("last_t", now)
            d["base_models"] = d.get("last_models") or models
            d["midnight"] = date.fromtimestamp(d["base_t"]).isoformat() != today
        else:
            d["base"], d["base_t"], d["base_models"] = cur, now, models
            d["midnight"] = False
            d.setdefault("days", [])
            d["yday"] = None
        mb = d.setdefault("model_bases", {})
        mb[today] = d["base_models"]
        for k in sorted(mb)[:-KEEP_MODEL_BASES]:
            del mb[k]
        # Keep the closed day's hourly buckets for best-time insights.
        if d.get("day") and d.get("hours"):
            hd = [x for x in d.get("hour_days", []) if x.get("date") != d["day"]]
            hd.append({"date": d["day"], "hours": d["hours"]})
            d["hour_days"] = hd[-KEEP_HOUR_DAYS:]
        d["day"] = today
        d["hours"] = {k: [0] * 24 for k in STATS}

    def add_events(self, events: list[dict[str, Any]], now: float | None = None) -> None:
        if not events:
            return
        now = now or time.time()
        with self._lock:
            feed = self.d.setdefault("feed", [])
            for e in events:
                feed.append({**e, "t": int(e.get("t", now))})
            self.d["feed"] = feed[-KEEP_FEED:]
            self._save()

    # ---- views -------------------------------------------------------------

    def today(self) -> dict[str, float]:
        with self._lock:
            if not self.d.get("base") or not self.d.get("last"):
                return {k: 0 for k in (*STATS, "points", *LATE)}
            return _diff(self.d["base"], self.d["last"])

    def insights(self, metrics: tuple[str, ...] = ("downloads", "prints", "boosts"),
                 window: int = 30, min_days: int = 7) -> dict[str, Any]:
        """Activity by hour of day and by weekday over the last `window` days
        (local time), from the stored per-day hourly buckets plus today."""
        with self._lock:
            days = list(self.d.get("hour_days", []))[-(window - 1):]
            if self.d.get("day"):
                days.append({"date": self.d["day"], "hours": self.d.get("hours") or {}})
        out: dict[str, Any] = {"days": len(days), "has": len(days) >= min_days}
        for m in metrics:
            hours, wday = [0] * 24, [0] * 7
            for x in days:
                h = (x.get("hours") or {}).get(m) or [0] * 24
                wd = date.fromisoformat(x["date"]).weekday()  # Mon = 0
                for i, v in enumerate(h[:24]):
                    hours[i] += int(v)
                wday[wd] += int(sum(h))
            out[m] = {"hour": hours, "wday": wday,
                      "bestHour": max(range(24), key=lambda i: hours[i]) if any(hours) else -1,
                      "bestDay": max(range(7), key=lambda i: wday[i]) if any(wday) else -1}
        return out

    def model_history(self, mid: str) -> list[dict[str, Any]]:
        """Daily activity of one model, oldest first, from the per-day model
        baselines (taken around midnight) plus today's live counts."""
        with self._lock:
            mb = dict(self.d.get("model_bases", {}))
            last = (self.d.get("last_models") or {}).get(mid)
        dates = sorted(mb)
        rows = []
        for i, dt in enumerate(dates):
            a = mb[dt].get(mid)
            b = mb[dates[i + 1]].get(mid) if i + 1 < len(dates) else last
            if not a or not b:
                continue
            rows.append({"d": dt, **{k: max(0, int(b.get(k, 0)) - int(a.get(k, 0))) for k in STATS}})
        return rows

    def view(self) -> dict[str, Any]:
        with self._lock:
            d = self.d
            today = self.today()
            days = list(d.get("days", []))[-6:]
            if d.get("day"):
                days.append({**today, "date": d["day"]})
            return {
                "today": today,
                "since": d.get("base_t", 0),
                "midnight": bool(d.get("midnight")),
                "yday": d.get("yday"),
                "hours": d.get("hours") or {k: [0] * 24 for k in STATS},
                "days": days,
                # Today's events only, newest first.
                "feed": [e for e in d.get("feed", []) if e.get("t", 0) >= d.get("base_t", 0)][-8:][::-1],
                "model_bases": d.get("model_bases", {}),
                "base_models": d.get("base_models", {}),
            }
