"""The opt-in daily summary: the day's totals, top models, spikes and the
To do items the dashboard shows, as one message."""
from __future__ import annotations

import statistics
from datetime import datetime
from typing import Any

from .events import MAKERWORLD
from .style import num, signed, stars

_STATS = (("downloads", "download", "downloads"), ("prints", "print", "prints"), ("boosts", "boost", "boosts"),
          ("likes", "like", "likes"), ("collects", "collect", "collects"))


def _link(r: dict[str, Any]) -> str:
    return f"[{r['n']}]({MAKERWORLD}/models/{r['id']})"


def _counts(d: dict[str, Any], sign: bool) -> str:
    parts = [f"{signed(d[k]) if sign else num(d[k])} {one if abs(d[k]) == 1 else many}"
             for k, one, many in _STATS if d.get(k)]
    return ", ".join(parts)


def build(today: dict[str, Any], yday: dict[str, Any] | None, full: dict[str, Any] | None,
          spiked: list[dict[str, Any]], now: datetime | None = None) -> tuple[str, list[str]]:
    """Title and lines (markdown links; channels without markdown strip them)."""
    now = now or datetime.now()
    lines = [_counts(today, True) or "No new downloads, prints or likes today."]
    if yday:
        y = _counts(yday, False)
        lines.append(f"Yesterday: {y}" if y else "Yesterday: nothing")
    extra = []
    if today.get("points"):
        extra.append(f"{signed(round(today['points'], 1))} points")
    if today.get("followers"):
        f = today["followers"]
        extra.append(f"{signed(f)} follower{'' if abs(f) == 1 else 's'}")
    if today.get("ratings"):
        n = today["ratings"]
        s = stars(today.get("rating_sum") or 0, n)
        extra.append(f"{num(n)} new rating{'' if n == 1 else 's'}" + (f" ({s}{'' if n == 1 else ' avg'})" if s else ""))
    if extra:
        lines.append(", ".join(extra))

    rows = list(((full or {}).get("models") or {}).values())
    top = sorted((r for r in rows if r.get("todayD")), key=lambda r: -r["todayD"])[:3]
    if top:
        lines.append("Most downloaded today: " + ", ".join(f"{_link(r)} {signed(r['todayD'])}" for r in top))
    for s in spiked[:3]:
        lines.append(f"📈 Spiked: {_link(s)}, {num(s['today'])} downloads against about {s['exp']:g} by then on a normal day")

    acct = (full or {}).get("acct") or {}
    if acct.get("unans"):
        n = acct["unans"]
        lines.append(f"💬 {num(n)} comment{'' if n == 1 else 's'} waiting for a reply")
    # The page most behind your typical downloads per view (the dashboard's "Pages to improve").
    has = [r for r in rows if r.get("has")]
    dpv = [r["dpv30"] for r in has if r["m"]["v"] >= 30 and r.get("dpv30") is not None]
    if dpv:
        md = statistics.median(dpv)
        under = [r for r in has if r["m"]["v"] >= 150 and r.get("dpv30") is not None and r["dpv30"] < md * 0.5]
        if under:
            r = max(under, key=lambda r: r["m"]["v"] * (md - r["dpv30"]))
            lines.append(f"🔧 Improve the page for {_link(r)}: {num(int(r['m']['v']))} views in 30 days, "
                         f"{r['dpv30'] * 100:.1f}% downloaded (typical {md * 100:.1f}%)")
    return f"Daily summary · {now.strftime('%A')}", lines
