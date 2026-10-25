"""Build panel section/page layouts from a snapshot.

Each page is a list of elements the firmware draws verbatim (physical px,
250x122, black header band covers y 0-17):
  {"t": str, "x", "y", "s": 12|16|24|32|48, "a": "l|c|r", "b": bold, "k": tracking, "inv": white}
  {"hl": y, "x0", "x1", "d": dotted}      horizontal rule
  {"vl": x, "y0", "y1", "d": dotted, "w"}  vertical rule
  {"rect": [x, y, w, h], "f": "s|h|o"}     solid / layer-line hatch / outline
A page may instead be {"title": str, "items": [...]} to override the header.
The firmware draws the header band (title, section index, page squares) and
inserts its own ALL TIME totals section right after TODAY.
Readability rules: anything worth reading is 16 px or larger (12 px only as a
fallback for labels that would not fit); no bold below 24 px, since the
double-strike fills in M and W.
"""

from __future__ import annotations

import time
import unicodedata
from datetime import date, datetime
from typing import Any

from .state import Snapshot

PANEL_W = 250
PANEL_H = 122
MARGIN = 6
CONTENT_TOP = 20          # header band is 0-17
COL_CENTERS = (42, 125, 208)
COL_W = 80
# Stat page rows: values (32 px; the panel's 48 px font is segment-style and hard to read),
# 16 px labels, layer-line rule, one 16 px footer line.
VALUE_Y = 24
LABEL_Y = 58
RULE_Y = 79
FOOTER_Y = 93


def _ascii(s: str) -> str:
    s = unicodedata.normalize("NFKD", str(s or ""))
    return "".join(ch for ch in s if " " <= ch <= "~")


def ascii_text(s: str) -> str:
    # Panel font only covers printable ASCII; also collapse whitespace.
    return " ".join(_ascii(s).split())


def width(s: str, size: int, k: int = 0, bold: bool = False) -> int:
    n = len(s)
    return n * (size // 2) + max(0, n - 1) * k + (1 if bold and n else 0) if n else 0


def fit(s: str, max_w: int, size: int, k: int = 0, bold: bool = False) -> str:
    s = _ascii(s)
    if width(s, size, k, bold) <= max_w:
        return s
    while len(s) > 1 and width(s + ".", size, k, bold) > max_w:
        s = s[:-1]
    return s.rstrip() + "."


def text(x: int, y: int, size: int, s: str, align: str = "l", bold: bool = False,
         k: int = 0, inv: bool = False, max_w: int | None = None) -> dict[str, Any]:
    if max_w is None:
        max_w = PANEL_W - MARGIN - x if align == "l" else x - MARGIN if align == "r" else PANEL_W
    el: dict[str, Any] = {"t": fit(s, max_w, size, k, bold), "x": x, "y": y, "s": size}
    if align != "l":
        el["a"] = align
    if bold:
        el["b"] = 1
    if k:
        el["k"] = k
    if inv:
        el["inv"] = 1
    return el


def wrap(s: str, max_w: int, size: int, max_lines: int) -> list[str]:
    cols = max(1, max_w // (size // 2))
    words = ascii_text(s).split()
    lines: list[str] = []
    cur = ""
    for w in words:
        while len(w) > cols:
            if cur:
                lines.append(cur)
                cur = ""
            lines.append(w[:cols])
            w = w[cols:]
        if not cur:
            cur = w
        elif len(cur) + 1 + len(w) <= cols:
            cur += " " + w
        else:
            lines.append(cur)
            cur = w
    if cur:
        lines.append(cur)
    if len(lines) > max_lines:
        lines = lines[:max_lines]
        lines[-1] = fit(lines[-1] + " ...", max_w, size)
    return lines


def label(x: int, y: int, s: str, max_w: int = COL_W) -> dict[str, Any]:
    """Centred caps label: 16 px when it fits, else small tracked caps."""
    s = s.upper()
    if width(s, 16) <= max_w:
        return text(x, y, 16, s, "c", max_w=max_w)
    k = 1 if width(s, 12, 1) <= max_w else 0
    return text(x, y + 2, 12, s, "c", k=k, max_w=max_w)


def stat_cells(cells: list[tuple[str, str]]) -> list[dict[str, Any]]:
    """Three big centred numbers (one size for the row) with labels and dividers."""
    values = [_ascii(v) for v, _ in cells]
    size = next((s for s in (32, 24) if all(width(v, s) <= COL_W - 4 for v in values)), 16)
    y = VALUE_Y + (32 - size) // 2
    out: list[dict[str, Any]] = []
    for cx, value, (_, lab) in zip(COL_CENTERS, values, cells):
        out.append(text(cx, y, size, value, "c", max_w=COL_W))
        out.append(label(cx, LABEL_Y, lab))
    out += [{"vl": x, "y0": VALUE_Y + 4, "y1": LABEL_Y + 14, "d": 1} for x in (83, 166)]
    return out


def stat_page(cells: list[tuple[str, str]], footer: str = "", title: str = "") -> Any:
    items = stat_cells(cells) + [{"rect": [0, RULE_Y, PANEL_W, 5], "f": "h"}]
    if footer:
        items.append(text(PANEL_W // 2, FOOTER_Y, 16, footer.upper(), "c"))
    return {"title": title, "items": items} if title else items


def fmt_num(v: float) -> str:
    v = float(v or 0)
    if v >= 100000:
        return f"{v / 1000:.0f}k"
    if v >= 10000:
        return f"{v / 1000:.1f}k"
    return f"{v:.0f}" if v == int(v) else f"{v:.1f}"


def signed(v: float) -> str:
    return ("+" if v >= 0 else "-") + fmt_num(abs(v))


def fmt_pts(v: float) -> str:
    return f"{v:.1f}" if v < 100 else f"{v:.0f}"


def fmt_day(iso: str) -> str:
    try:
        if "T" in iso:
            d = datetime.fromisoformat(iso.replace("Z", "+00:00")).date()
        else:
            d = date.fromisoformat(iso)
        return d.strftime("%b %d")
    except ValueError:
        return ""


def today_pages(day: dict[str, Any] | None) -> list[Any]:
    """Since midnight, from DayTracker.view(). Yesterday's full day as the footer."""
    if not day:
        return []
    t, y = day["today"], day.get("yday")
    title = "TODAY" if day.get("midnight") else f"TODAY SINCE {datetime.fromtimestamp(day['since']):%H:%M}"

    def yday(*keys: str) -> str:
        if not y:
            return ""
        return "YESTERDAY " + " / ".join(fmt_num(y.get(k, 0)) for k in keys)

    return [
        stat_page([(fmt_num(t["downloads"]), "downloads"), (fmt_num(t["prints"]), "prints"),
                   (fmt_num(t["boosts"]), "boosts")], yday("downloads", "prints", "boosts"), title),
        stat_page([(fmt_num(t["likes"]), "likes"), (fmt_num(t["collects"]), "saves"),
                   (fmt_pts(t["points"]), "points")], yday("likes", "collects", "points"), title),
    ]


def account_pages(snap: Snapshot) -> list[Any]:
    a = snap.extras.get("account") or {}
    if not a:
        return []
    return [stat_page(
        [(fmt_num(a.get("followers", 0)), "followers"), (fmt_num(a.get("likes", 0)), "likes"),
         (fmt_num(a.get("saves", 0)), "saves")],
        f"{a.get('models', 0)} models / {a.get('comments', 0)} comments",
    )]


def month_pages(snap: Snapshot) -> list[Any]:
    m = snap.extras.get("month") or {}
    if not m:
        return []
    impressions = m.get("impressions", 0)
    rate = f"{100 * m.get('views', 0) / impressions:.1f}%" if impressions else "-"
    reach = stat_page(
        [(fmt_num(impressions), "impressions"), (fmt_num(m.get("views", 0)), "views"), (rate, "view rate")],
        f"{signed(m.get('followers', 0))} followers  {signed(m.get('likes', 0))} likes",
    )
    earned = stat_page(
        [(fmt_num(m.get("downloads", 0)), "downloads"), (fmt_num(m.get("prints", 0)), "prints"),
         (fmt_num(m.get("boosts", 0)), "boosts")],
        f"{signed(m.get('points', 0))} pts  {signed(m.get('saves', 0))} saves",
    )
    return [reach, earned]


def top_model_pages(snap: Snapshot, count: int = 5, history: Any = None) -> list[Any]:
    """One model per page: name in two lines, then its three main counts.
    With `history` (model id -> daily rows), each model gets a second page
    with its last 7 and 30 days (OK steps to it)."""
    ranked = sorted(snap.models.items(), key=lambda kv: (kv[1].downloads + kv[1].prints, kv[1].boosts),
                    reverse=True)[:count]
    pages: list[Any] = []
    for i, (mid, m) in enumerate(ranked):
        items: list[dict[str, Any]] = []
        for j, line in enumerate(wrap(m.name, PANEL_W - 2 * MARGIN, 16, 2)):
            items.append(text(MARGIN, CONTENT_TOP + 1 + j * 18, 16, line))
        cells = [(fmt_num(m.downloads), "downloads"), (fmt_num(m.prints), "prints"), (fmt_num(m.boosts), "boosts")]
        size = 32 if all(width(v, 32) <= COL_W - 4 for v, _ in cells) else 24
        for cx, (v, lab) in zip(COL_CENTERS, cells):
            items.append(text(cx, 64 + (32 - size) // 2, size, v, "c", max_w=COL_W))
            items.append(label(cx, 99, lab))
        items.append({"hl": 59, "x0": 0, "x1": PANEL_W - 1, "d": 1})
        items += [{"vl": x, "y0": 66, "y1": 114, "d": 1} for x in (83, 166)]
        pages.append({"title": f"TOP MODEL #{i + 1}", "items": items})
        if history is not None:
            pages.append(model_detail_page(i, m.name, history(mid)))
    return pages


def model_detail_page(i: int, name: str, rows: list[dict[str, Any]]) -> Any:
    """Last 7 / 30 days of one model: name, then a small table."""
    items: list[dict[str, Any]] = [text(MARGIN, CONTENT_TOP + 1, 16, fit(ascii_text(name), PANEL_W - 2 * MARGIN, 16))]
    items.append(text(150, 40, 12, "7 DAYS", "c"))
    items.append(text(215, 40, 12, "30 DAYS", "c"))
    items.append({"hl": 53, "x0": 0, "x1": PANEL_W - 1, "d": 1})
    last7, last30 = rows[-7:], rows[-30:]
    for j, (k, lab) in enumerate((("downloads", "Downloads"), ("prints", "Prints"), ("boosts", "Boosts"))):
        y = 57 + j * 18
        items.append(text(MARGIN, y, 16, lab))
        items.append(text(150, y, 16, signed(sum(r.get(k, 0) for r in last7)), "c"))
        items.append(text(215, y, 16, signed(sum(r.get(k, 0) for r in last30)), "c"))
    if len(rows) < 30:
        items.append(text(PANEL_W // 2, 111, 12, f"{len(rows)} day(s) of history so far", "c"))
    return {"title": f"MODEL #{i + 1} - 7/30 DAYS", "items": items}


WEEKDAYS = ("MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN")


def best_time_pages(ins: dict[str, Any] | None) -> list[Any]:
    """When activity happens (last 30 days): hour bars, then a weekday row."""
    if not ins or not ins.get("has"):
        days = (ins or {}).get("days", 0)
        return [[text(PANEL_W // 2, 50, 16, f"Collecting data ({days} of 7 days)", "c")]]
    pages: list[Any] = []
    for key, lab in (("downloads", "DOWNLOADS"), ("prints", "PRINTS")):
        m = ins.get(key) or {}
        hours, wday = m.get("hour") or [0] * 24, m.get("wday") or [0] * 7
        bh, bd = m.get("bestHour", -1), m.get("bestDay", -1)
        mx, base, top = max(1, max(hours)), 84, 34
        items: list[dict[str, Any]] = [text(MARGIN, CONTENT_TOP + 1, 12, f"{lab} BY HOUR", k=1)]
        for h, v in enumerate(hours):
            hh = 2 + int((base - top - 2) * v / mx) if v else 1
            items.append({"rect": [MARGIN + h * 10, base - hh, 7, hh], "f": "s" if h == bh else "h"})
        items.append({"hl": base + 1, "x0": MARGIN, "x1": MARGIN + 24 * 10 - 3})
        for h, t in ((0, "12a"), (6, "6a"), (12, "12p"), (18, "6p")):
            items.append(text(MARGIN + h * 10, base + 3, 12, t))
        best = (f"{bh % 12 or 12}{'am' if bh < 12 else 'pm'}" if bh >= 0 else "-") + "  " + (WEEKDAYS[bd] if bd >= 0 else "-")
        items.append(text(PANEL_W - MARGIN, CONTENT_TOP + 1, 12, f"BEST {best}", "r"))
        pages.append({"title": f"BEST TIMES - {lab}", "items": items})
        # Weekday row: name over value, best day inverted.
        row: list[dict[str, Any]] = [text(MARGIN, CONTENT_TOP + 1, 12, f"{lab} BY WEEKDAY", k=1)]
        cw = (PANEL_W - 2 * MARGIN) // 7
        for d, v in enumerate(wday):
            x = MARGIN + d * cw
            inv = d == bd
            if inv:
                row.append({"rect": [x, 44, cw - 2, 50], "f": "s"})
            row.append(text(x + cw // 2 - 1, 50, 12, WEEKDAYS[d], "c", inv=inv))
            row.append(text(x + cw // 2 - 1, 68, 16, fmt_num(v), "c", inv=inv))
        pages.append({"title": f"BEST TIMES - {lab}", "items": row})
    return pages


def comment_pages(snap: Snapshot, count: int = 5) -> list[Any]:
    """One comment per page: who and when, which model, then up to 3 lines of text."""
    pages: list[Any] = []
    for c in (snap.extras.get("comments") or [])[:count]:
        when = fmt_day(c.get("time", "")).upper()
        page = [
            text(MARGIN, CONTENT_TOP + 1, 16, ascii_text(c.get("user", "")),
                 max_w=PANEL_W - 2 * MARGIN - width(when, 16) - 8),
            text(PANEL_W - MARGIN, CONTENT_TOP + 1, 16, when, "r"),
            text(MARGIN, CONTENT_TOP + 19, 16, ascii_text(c.get("model", ""))),
            {"hl": CONTENT_TOP + 38, "x0": 0, "x1": PANEL_W - 1, "d": 1},
        ]
        body_x = MARGIN + 8
        lines = wrap(c.get("text", ""), PANEL_W - MARGIN - body_x, 16, 3)
        y0 = CONTENT_TOP + 44
        page.append({"vl": MARGIN, "y0": y0, "y1": y0 + 18 * len(lines) - 3, "w": 2})
        for i, line in enumerate(lines):
            page.append(text(body_x, y0 + i * 18, 16, line))
        pages.append(page)
    return pages


def build_sections(snap: Snapshot, cycle_sec: int, day: dict[str, Any] | None = None,
                   insights: dict[str, Any] | None = None, model_history: Any = None) -> dict[str, Any]:
    # The firmware inserts its ALL TIME totals section after TODAY; tz is this
    # host's UTC offset, which the e-ink panel uses for quiet hours.
    return {
        "cycle": cycle_sec,
        "tz": time.localtime().tm_gmtoff,
        "sections": [
            {"name": "TODAY", "pages": today_pages(day)},
            {"name": "LAST 30 DAYS", "pages": month_pages(snap)},
            {"name": "TOP MODELS", "pages": top_model_pages(snap, history=model_history)},
            {"name": "BEST TIMES", "pages": best_time_pages(insights)},
            {"name": "COMMENTS", "pages": comment_pages(snap)},
            {"name": "ACCOUNT", "pages": account_pages(snap)},
        ],
    }
