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
_TOP = 5   # models under Top models today
_TODO = 5  # To do entries


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
           for r in sorted((r for r in rows if r.get("todayD")), key=lambda r: -r["todayD"])[:_TOP]]
    for t in top:
        s = spikes.get(t["id"])
        if s:
            t["x"] = s.get("x") or round(s["today"] / max(s.get("exp") or 0, 0.5), 1)

    todo = []
    if acct.get("unans"):
        n = acct["unans"]
        todo.append(f"💬 {num(n)} comment{'' if n == 1 else 's'} waiting for a reply")
    # The pages most behind your typical downloads per view (the dashboard's "Pages to improve").
    has = [r for r in rows if r.get("has")]
    dpv = [r["dpv30"] for r in has if r["m"]["v"] >= 30 and r.get("dpv30") is not None]
    if dpv:
        md = statistics.median(dpv)
        under = [r for r in has if r["m"]["v"] >= 150 and r.get("dpv30") is not None and r["dpv30"] < md * 0.5]
        for r in sorted(under, key=lambda r: -r["m"]["v"] * (md - r["dpv30"]))[:_TODO - 1]:
            todo.append(f"🔧 Improve [{_short(r['n'])}]({_url(r['id'])}): {r['dpv30'] * 100:.1f}% of "
                        f"{num(int(r['m']['v']))} views downloaded (typical {md * 100:.1f}%)")
    # Fill the rest with the models that have the most unanswered comments.
    for r in sorted((r for r in rows if r.get("unans")), key=lambda r: -r["unans"])[:max(0, _TODO - len(todo))]:
        todo.append(f"💬 Reply on [{_short(r['n'])}]({_url(r['id'])}): {num(r['unans'])} waiting")

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


def _today(v: float, y: float | None) -> str:
    """"19 ▲ (+3 from 16)": today, an up/down indicator, the change from yesterday."""
    if y is None:
        return num(v)
    d = v - y
    if not d:
        return f"{num(v)} = (same as yesterday)"
    return f"{num(v)} {'▲' if d > 0 else '▼'} ({signed(d)} from {num(y)})"


def _top_line(i: int, t: dict[str, Any]) -> str:
    line = f"{i}. **{signed(t['d'])}** [{_short(t['n'])}]({_url(t['id'])})"
    if t.get("x"):
        line += f" · 📈 {t['x']:g}× usual"
    return line


def _ratings(r: dict[str, Any]) -> str:
    """"1 new (5★)" or "3 new (4.3★ avg)"."""
    n = r["ratings"]
    return f"{num(n)} new" + (f" ({r['stars']}{'' if n == 1 else ' avg'})" if r["stars"] else "")


def _points(r: dict[str, Any]) -> str:
    return signed(r["points"]) + (f" (7-day avg {num(r['points7'])})" if r.get("points7") else "")


_RULE = "━━━━━━━━━━━━━━━━"


def lines(r: dict[str, Any]) -> list[str]:
    """The report as text: a heading and a rule per section, a blank line between."""
    out = ["📅 Today", _RULE]
    if any(s["v"] for s in r["stats"]) or any(s["y"] for s in r["stats"]):
        out += [f"{s['e']} {s['label']}: {_today(s['v'], s['y'])}" for s in r["stats"]]
    else:
        out.append("No new downloads, prints or likes today.")
    if r["points"]:
        out.append(f"⭐ Points: {_points(r)}")
    if r["ratings"]:
        out.append(f"🌟 Ratings: {_ratings(r)}")
    if r["followers"]:
        out.append(f"👤 Followers: {signed(r['followers'])}")
    if r["top"]:
        out += ["", "🔥 Top models today", _RULE] + [_top_line(i, t).replace("**", "") for i, t in enumerate(r["top"], 1)]
    if r["todo"]:
        out += ["", "📝 To do", _RULE] + r["todo"]
    return out


_GAP = "\n\u200b"  # ends a section's values with a blank line, to space the sections apart


def fields(r: dict[str, Any]) -> list[dict[str, Any]]:
    """Discord embed fields: a grid of today's numbers, then the lists."""
    out = []
    for s in r["stats"]:
        t = _today(s["v"], s["y"])
        n = num(s["v"])
        out.append({"name": f"{s['e']} {s['label']}", "value": f"**{n}**{t[len(n):]}", "inline": True})
    if r["points"]:
        p = _points(r)
        n = signed(r["points"])
        out.append({"name": "⭐ Points", "value": f"**{n}**{p[len(n):]}", "inline": True})
    if r["ratings"]:
        out.append({"name": "🌟 Ratings", "value": f"**{_ratings(r)}**", "inline": True})
    if r["followers"]:
        out.append({"name": "👤 Followers", "value": f"**{signed(r['followers'])}**", "inline": True})
    while sum(1 for f in out if f["inline"]) % 3:  # keep the grid's last row aligned
        out.append({"name": "\u200b", "value": "\u200b", "inline": True})
    lists = []
    if r["top"]:
        lists.append({"name": "🔥 Top models today",
                      "value": _fit([_top_line(i, t) for i, t in enumerate(r["top"], 1)]), "inline": False})
    if r["todo"]:
        lists.append({"name": "📝 To do", "value": _fit(r["todo"]), "inline": False})
    # Every section but the last ends with a blank line: the grid's last row, then each list.
    for f in (out[-3:] if lists else []) + lists[:-1]:
        f["value"] += _GAP
    return out + lists


def _fit(rows: list[str], limit: int = 1024 - len(_GAP)) -> str:
    """Whole lines only, within Discord's field limit."""
    out = ""
    for ln in rows:
        if len(out) + len(ln) + 1 > limit:
            break
        out += ("\n" if out else "") + ln
    return out


def sample() -> dict[str, Any]:
    """A made-up report for the test message."""
    full = {"acct": {"unans": 3, "pts7avg": 12.4},
            "models": {"1": {"id": 123456, "n": "Sample Model", "todayD": 9, "unans": 2},
                       "2": {"id": 654321, "n": "Another Model", "todayD": 4, "unans": 1}}}
    today = {"downloads": 17, "prints": 9, "likes": 5, "collects": 24, "points": 14.2, "ratings": 1, "rating_sum": 5}
    yday = {"downloads": 18, "prints": 6, "likes": 5, "collects": 20}
    spiked = [{"id": 123456, "n": "Sample Model", "today": 9, "exp": 1.5, "x": 6}]
    return build(today, yday, full, spiked, datetime.fromtimestamp(time.time()))
