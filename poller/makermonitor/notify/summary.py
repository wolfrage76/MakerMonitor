"""The opt-in daily summary: the day's totals against yesterday, the top and
spiking models, and the To do items the dashboard shows.

build() collects the numbers into a plain dict (the "report"). lines() renders
it as text for every channel (markdown links; channels without markdown strip
them) and fields() as Discord embed fields.
"""
from __future__ import annotations

import statistics
import time
from datetime import datetime, timedelta
from typing import Any

from .events import MAKERWORLD
from .style import num, signed, stars

# key in the daily totals, emoji, label: always shown, in this order.
_STATS = (("downloads", "⬇️", "Downloads"), ("prints", "🖨️", "Prints"), ("likes", "❤️", "Likes"),
          ("collects", "📌", "Collects"), ("boosts", "🚀", "Boosts"), ("points", "⭐", "Points"),
          ("ratings", "🌟", "Ratings"))
_NAME_MAX = 48
_TOP = 5   # models under Top models today
_TODO = 5  # To do entries
_TODO_COMMENTS = 3  # of which new unanswered comments (the rest: pages to improve)
_QUOTE_MAX = 80


def _short(name: str, n: int = _NAME_MAX) -> str:
    name = " ".join(str(name).split())
    return name if len(name) <= n else name[:n - 1].rstrip() + "…"


def _url(mid: Any) -> str:
    return f"{MAKERWORLD}/models/{mid}"


def _clock(t: datetime) -> str:
    return t.strftime("%I:%M %p").lstrip("0")


def _ts(iso: Any) -> float:
    try:
        return datetime.fromisoformat(str(iso).replace("Z", "+00:00")).timestamp()
    except ValueError:
        return 0.0


def _when(ts: float, now: datetime) -> str:
    """"3:12 PM" today, "yesterday 8:05 PM" before that."""
    t = datetime.fromtimestamp(ts)
    return _clock(t) if t.date() == now.date() else f"yesterday {_clock(t)}"


def build(today: dict[str, Any], yday: dict[str, Any] | None, full: dict[str, Any] | None,
          spiked: list[dict[str, Any]], now: datetime | None = None,
          unanswered: list[dict[str, Any]] | None = None) -> dict[str, Any]:
    """The summary's numbers, ready for lines() and fields().

    `unanswered`: comments still waiting for a reply (creator.unanswered()); only
    those posted since the start of yesterday go in To do.
    """
    now = now or datetime.now()
    acct = (full or {}).get("acct") or {}
    stats = []
    for k, emoji, label in _STATS:
        v, y = today.get(k) or 0, (yday.get(k) or 0) if yday else None
        note = ""
        if k == "points":
            v, y = round(v, 1), round(y, 1) if y is not None else None
            note = f"7d avg {num(acct['pts7avg'])}" if acct.get("pts7avg") else ""
        elif k == "ratings" and v:
            note = stars(today.get("rating_sum") or 0, v) + ("" if v == 1 else " avg")
        stats.append({"k": k, "e": emoji, "label": label, "v": v, "y": y, "note": note})

    rows = list(((full or {}).get("models") or {}).values())
    spikes = {str(s["id"]): s for s in spiked}
    top = [{"id": str(r["id"]), "n": r["n"], "d": r["todayD"]}
           for r in sorted((r for r in rows if r.get("todayD")), key=lambda r: -r["todayD"])[:_TOP]]
    for t in top:
        s = spikes.get(t["id"])
        if s:
            t["x"] = s.get("x") or round(s["today"] / max(s.get("exp") or 0, 0.5), 1)

    # To do: two-line entries (a bold headline, then "↳ detail").
    todo = []
    since = datetime.combine(now.date() - timedelta(days=1), datetime.min.time()).timestamp()
    recent = sorted((c for c in unanswered or [] if _ts(c.get("t")) >= since), key=lambda c: -_ts(c.get("t")))
    for c in recent[:_TODO_COMMENTS]:
        text = " ".join(str(c.get("x") or "").split())
        text = text if len(text) <= _QUOTE_MAX else text[:_QUOTE_MAX - 1].rstrip() + "…"
        todo.append(f"💬 **Reply to {c.get('u') or 'someone'}** on [{_short(c.get('m') or c.get('mid'), 36)}]"
                    f"({_url(c.get('mid'))})\n↳ {_when(_ts(c.get('t')), now)} · “{text}”")
    if len(recent) > _TODO_COMMENTS:
        more = len(recent) - _TODO_COMMENTS
        todo[-1] += f"\n↳ +{more} more new comment{'' if more == 1 else 's'} waiting"
    # The pages most behind your typical downloads per view (the dashboard's "Pages to improve").
    has = [r for r in rows if r.get("has")]
    dpv = [r["dpv30"] for r in has if r["m"]["v"] >= 30 and r.get("dpv30") is not None]
    if dpv:
        md = statistics.median(dpv)
        under = [r for r in has if r["m"]["v"] >= 150 and r.get("dpv30") is not None and r["dpv30"] < md * 0.5]
        for r in sorted(under, key=lambda r: -r["m"]["v"] * (md - r["dpv30"]))[:max(0, _TODO - len(todo))]:
            todo.append(f"🔧 **Improve** [{_short(r['n'])}]({_url(r['id'])})\n↳ {r['dpv30'] * 100:.1f}% of "
                        f"{num(int(r['m']['v']))} views downloaded in 30 days (typical {md * 100:.1f}%)")

    return {
        "title": f"Daily summary · {now.strftime('%A, %b')} {now.day}",
        "time": _clock(now),
        "stats": stats,
        "followers": today.get("followers") or 0,
        "top": top,
        "todo": todo,
    }


def _today(v: float, y: float | None) -> str:
    """"19 ▲ (+3 from 16)": today, an up/down indicator, the change from yesterday."""
    if y is None:
        return num(v)
    d = round(v - y, 2)
    if not d:
        return f"{num(v)} = (same as yesterday)"
    return f"{num(v)} {'▲' if d > 0 else '▼'} ({signed(d)} from {num(y)})"


def _top_line(i: int, t: dict[str, Any]) -> str:
    line = f"{i}. **{signed(t['d'])}** [{_short(t['n'])}]({_url(t['id'])})"
    if t.get("x"):
        line += f" · 📈 {t['x']:g}× usual"
    return line


_RULE = "━━━━━━━━━━━━━━━━"


def lines(r: dict[str, Any]) -> list[str]:
    """The report as text: a heading and a rule per section, a blank line between."""
    out = ["📅 Today", _RULE]
    if any(s["v"] for s in r["stats"]) or any(s["y"] for s in r["stats"]):
        out += [f"{s['e']} {s['label']}: {_today(s['v'], s['y'])}" + (f" · {s['note']}" if s["note"] else "")
                for s in r["stats"]]
    else:
        out.append("No new downloads, prints or likes today.")
    if r["followers"]:
        out.append(f"👤 Followers: {signed(r['followers'])}")
    if r["top"]:
        out += ["", "🔥 Top models today", _RULE] + [_top_line(i, t).replace("**", "") for i, t in enumerate(r["top"], 1)]
    if r["todo"]:
        out += ["", "📝 To do", _RULE]
        for i, t in enumerate(r["todo"]):
            out += ([""] if i else []) + t.replace("**", "").split("\n")  # a blank line between entries
    return out


_GAP = "\n\u200b"  # ends a section's values with a blank line, to space the sections apart


def fields(r: dict[str, Any]) -> list[dict[str, Any]]:
    """Discord embed fields: a grid of today's numbers, then the lists."""
    out = []
    for s in r["stats"]:
        t = _today(s["v"], s["y"])
        n = num(s["v"])
        out.append({"name": f"{s['e']} {s['label']}", "inline": True,
                    "value": f"**{n}**{t[len(n):]}" + (f"\n{s['note']}" if s["note"] else "")})
    if r["followers"]:
        out.append({"name": "👤 Followers", "value": f"**{signed(r['followers'])}**", "inline": True})
    while sum(1 for f in out if f["inline"]) % 3:  # keep the grid's last row aligned
        out.append({"name": "\u200b", "value": "\u200b", "inline": True})
    lists = []
    if r["top"]:
        lists.append({"name": "🔥 Top models today",
                      "value": _fit([_top_line(i, t) for i, t in enumerate(r["top"], 1)]), "inline": False})
    if r["todo"]:
        lists.append({"name": "📝 To do", "value": _fit(r["todo"], sep="\n\u200b\n"), "inline": False})
    # Every section but the last ends with a blank line: the grid's last row, then each list.
    for f in (out[-3:] if lists else []) + lists[:-1]:
        f["value"] += _GAP
    return out + lists


def _fit(rows: list[str], limit: int = 1024 - len(_GAP), sep: str = "\n") -> str:
    """Whole entries only, within Discord's field limit."""
    out = ""
    for ln in rows:
        if len(out) + len(ln) + len(sep) > limit:
            break
        out += (sep if out else "") + ln
    return out


def sample() -> dict[str, Any]:
    """A made-up report for the test message."""
    full = {"acct": {"unans": 3, "pts7avg": 12.4},
            "models": {"1": {"id": 123456, "n": "Sample Model", "todayD": 9, "unans": 2},
                       "2": {"id": 654321, "n": "Another Model", "todayD": 4, "unans": 1}}}
    today = {"downloads": 17, "prints": 9, "likes": 5, "collects": 24, "points": 14.2, "ratings": 1, "rating_sum": 5}
    yday = {"downloads": 18, "prints": 6, "likes": 5, "collects": 20}
    spiked = [{"id": 123456, "n": "Sample Model", "today": 9, "exp": 1.5, "x": 6}]
    now = datetime.fromtimestamp(time.time())
    unans = [{"mid": "123456", "m": "Sample Model", "u": "A Maker", "x": "Printed great, thanks! Any tips for PETG?",
              "t": datetime.fromtimestamp(time.time() - 3600).astimezone().isoformat()}]
    return build(today, yday, full, spiked, now, unans)
