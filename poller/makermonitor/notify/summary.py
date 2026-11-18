"""The opt-in daily summary: the day's totals against yesterday, the top and
spiking models, and the To do items the dashboard shows.

build() collects the numbers into a plain dict (the "report"). lines() renders
it as text for every channel (markdown links; channels without markdown strip
them) and fields() as Discord embed fields.
"""
from __future__ import annotations

import statistics
import time
from datetime import datetime
from typing import Any

from .events import MAKERWORLD
from .style import num, signed, stars

# key in the daily totals, emoji, label. Boosts are shown only when there were any.
_STATS = (("downloads", "⬇️", "Downloads"), ("prints", "🖨️", "Prints"), ("likes", "❤️", "Likes"),
          ("collects", "📌", "Collects"), ("boosts", "🚀", "Boosts"))
_NAME_MAX = 48


def _short(name: str) -> str:
    name = " ".join(str(name).split())
    return name if len(name) <= _NAME_MAX else name[:_NAME_MAX - 1].rstrip() + "…"


def _url(mid: Any) -> str:
    return f"{MAKERWORLD}/models/{mid}"


def _clock(t: datetime) -> str:
    return t.strftime("%I:%M %p").lstrip("0")


def build(today: dict[str, Any], yday: dict[str, Any] | None, full: dict[str, Any] | None,
          spiked: list[dict[str, Any]], now: datetime | None = None) -> dict[str, Any]:
    """The summary's numbers, ready for lines() and fields()."""
    now = now or datetime.now()
    acct = (full or {}).get("acct") or {}
    stats = []
    for k, emoji, label in _STATS:
        v, y = today.get(k) or 0, (yday.get(k) or 0) if yday else None
        if k == "boosts" and not v and not y:
            continue
        stats.append({"k": k, "e": emoji, "label": label, "v": v, "y": y})

    rows = list(((full or {}).get("models") or {}).values())
    spikes = {str(s["id"]): s for s in spiked}
    top = [{"id": str(r["id"]), "n": r["n"], "d": r["todayD"]}
           for r in sorted((r for r in rows if r.get("todayD")), key=lambda r: -r["todayD"])[:3]]
    have = {t["id"] for t in top}
    top += [{"id": str(s["id"]), "n": s["n"], "d": s["today"]} for s in spiked if str(s["id"]) not in have]
    top = top[:5]
    for t in top:
        s = spikes.get(t["id"])
        if s:
            t["x"] = s.get("x") or round(s["today"] / max(s.get("exp") or 0, 0.5), 1)

    todo = []
    if acct.get("unans"):
        n = acct["unans"]
        todo.append(f"💬 {num(n)} comment{'' if n == 1 else 's'} waiting for a reply")
    # The page most behind your typical downloads per view (the dashboard's "Pages to improve").
    has = [r for r in rows if r.get("has")]
    dpv = [r["dpv30"] for r in has if r["m"]["v"] >= 30 and r.get("dpv30") is not None]
    if dpv:
        md = statistics.median(dpv)
        under = [r for r in has if r["m"]["v"] >= 150 and r.get("dpv30") is not None and r["dpv30"] < md * 0.5]
        if under:
            r = max(under, key=lambda r: r["m"]["v"] * (md - r["dpv30"]))
            todo.append(f"🔧 Improve [{_short(r['n'])}]({_url(r['id'])}): {r['dpv30'] * 100:.1f}% of "
                        f"{num(int(r['m']['v']))} views downloaded in 30 days (typical {md * 100:.1f}%)")

    n = today.get("ratings") or 0
    return {
        "title": f"Daily summary · {now.strftime('%A, %b')} {now.day}",
        "time": _clock(now),
        "stats": stats,
        "points": round(today.get("points") or 0, 1),
        "points7": acct.get("pts7avg"),
        "followers": today.get("followers") or 0,
        "ratings": n,
        "stars": stars(today.get("rating_sum") or 0, n) if n else "",
        "top": top,
        "todo": todo,
    }


def _delta(v: float, y: float | None) -> str:
    if y is None:
        return ""
    d = v - y
    return f"▲ {num(d)}" if d > 0 else f"▼ {num(-d)}" if d < 0 else "same"


def _top_line(i: int, t: dict[str, Any]) -> str:
    line = f"{i}. **{signed(t['d'])}** [{_short(t['n'])}]({_url(t['id'])})"
    if t.get("x"):
        line += f" · 📈 {t['x']:g}× usual"
    return line


def _extras(r: dict[str, Any]) -> list[str]:
    out = []
    if r["points"]:
        p = f"⭐ Points: {signed(r['points'])}"
        if r.get("points7"):
            p += f" (7-day avg {num(r['points7'])})"
        out.append(p)
    if r["ratings"]:
        out.append(f"🌟 Ratings: {signed(r['ratings'])}" + (f" ({r['stars']}{'' if r['ratings'] == 1 else ' avg'})"
                                                            if r["stars"] else ""))
    if r["followers"]:
        out.append(f"👤 Followers: {signed(r['followers'])}")
    return out


def lines(r: dict[str, Any]) -> list[str]:
    """The report as text, one block per section with a blank line between."""
    out = []
    if any(s["v"] for s in r["stats"]) or any(s["y"] for s in r["stats"]):
        out.append("Today" + (" (vs yesterday)" if any(s["y"] is not None for s in r["stats"]) else ""))
        for s in r["stats"]:
            d = _delta(s["v"], s["y"])
            out.append(f"{s['e']} {s['label']}: {num(s['v'])}"
                       + (f"  {d} from {num(s['y'])}" if d and d != "same" else "  same as yesterday" if d else ""))
    else:
        out.append("No new downloads, prints or likes today.")
    out += _extras(r)
    if r["top"]:
        out += ["", "🔥 Top models today"] + [_top_line(i, t).replace("**", "") for i, t in enumerate(r["top"], 1)]
    if r["todo"]:
        out += ["", "📝 To do"] + r["todo"]
    return out


def fields(r: dict[str, Any]) -> list[dict[str, Any]]:
    """Discord embed fields: a grid of today's numbers, then the lists."""
    out = []
    for s in r["stats"]:
        d = _delta(s["v"], s["y"])
        val = f"**{num(s['v'])}**" + (f"  {d}" if d and d != "same" else "")
        if s["y"] is not None:
            val += f"\nyesterday {num(s['y'])}"
        out.append({"name": f"{s['e']} {s['label']}", "value": val, "inline": True})
    if r["points"]:
        val = f"**{signed(r['points'])}**" + (f"\n7-day avg {num(r['points7'])}" if r.get("points7") else "")
        out.append({"name": "⭐ Points", "value": val, "inline": True})
    if r["ratings"]:
        out.append({"name": "🌟 Ratings", "value": f"**{signed(r['ratings'])}**" + (
            f"\n{r['stars']}{'' if r['ratings'] == 1 else ' avg'}" if r["stars"] else ""), "inline": True})
    if r["followers"]:
        out.append({"name": "👤 Followers", "value": f"**{signed(r['followers'])}**", "inline": True})
    while sum(1 for f in out if f["inline"]) % 3:  # keep the grid's last row aligned
        out.append({"name": "​", "value": "​", "inline": True})
    if r["top"]:
        out.append({"name": "🔥 Top models today",
                    "value": "\n".join(_top_line(i, t) for i, t in enumerate(r["top"], 1))[:1024], "inline": False})
    if r["todo"]:
        out.append({"name": "📝 To do", "value": "\n".join(r["todo"])[:1024], "inline": False})
    return out


def sample() -> dict[str, Any]:
    """A made-up report for the test message."""
    full = {"acct": {"unans": 3, "pts7avg": 12.4},
            "models": {"1": {"id": 123456, "n": "Sample Model", "todayD": 9},
                       "2": {"id": 654321, "n": "Another Model", "todayD": 4}}}
    today = {"downloads": 17, "prints": 9, "likes": 5, "collects": 24, "points": 14.2, "ratings": 1, "rating_sum": 5}
    yday = {"downloads": 18, "prints": 6, "likes": 5, "collects": 20}
    spiked = [{"id": 123456, "n": "Sample Model", "today": 9, "exp": 1.5, "x": 6}]
    return build(today, yday, full, spiked, datetime.fromtimestamp(time.time()))
