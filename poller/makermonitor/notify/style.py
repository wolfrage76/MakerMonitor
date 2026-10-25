"""How each event type looks: emoji, color, wording. Shared by all channels."""
from __future__ import annotations

import re
from dataclasses import dataclass, field

from .events import EVENT_TYPES, MAKERWORLD, Event


@dataclass(frozen=True)
class Style:
    emoji: str
    color: int  # 0xRRGGBB, used by channels that support color
    one: str
    many: str

    def label(self, n: float) -> str:
        return self.one if abs(n) == 1 else self.many


STYLES = {
    "download": Style("⬇️", 0x3B82F6, "download", "downloads"),
    "print": Style("🖨️", 0x10B981, "print", "prints"),
    "boost": Style("🚀", 0xF59E0B, "boost", "boosts"),
    "like": Style("❤️", 0xEF4444, "like", "likes"),
    "collect": Style("📌", 0x8B5CF6, "collect", "collects"),
    "comment": Style("💬", 0x06B6D4, "comment", "comments"),
    "follower": Style("👤", 0xEC4899, "follower", "followers"),
    "points": Style("⭐", 0xEAB308, "point", "points"),
    "health": Style("⚠️", 0xF97316, "alert", "alerts"),
}
HEALTH_OK = Style("✅", 0x22C55E, "alert", "alerts")
HEALTH_WARN = STYLES["health"]
QUIET = Style("🌙", 0x6366F1, "digest", "digests")


def num(n: float) -> str:
    return f"{n:,.2f}".rstrip("0").rstrip(".") if isinstance(n, float) and not n.is_integer() else f"{int(n):,}"


def signed(n: float) -> str:
    return ("+" if n > 0 else "") + num(n)


@dataclass
class Section:
    """One block of a message: a group of same-type events, a comment or an alert."""
    type: str
    style: Style
    title: str
    lines: list[str] = field(default_factory=list)
    url: str = ""
    author: str = ""  # comment author
    footer: str = ""


@dataclass
class Digest:
    """Everything one poll produced for one channel, ready to render."""
    summary: str  # one line, e.g. "+12 downloads, +3 likes, 1 comment"
    sections: list[Section]
    events: list[Event]
    heading: str = ""  # set for the end-of-quiet-hours digest


def merge(events: list[Event]) -> list[Event]:
    """Same stat on the same model (e.g. held across polls) becomes one event."""
    out: list[Event] = []
    seen: dict[tuple[str, str], Event] = {}
    for e in events:
        key = (e.type, e.model_id or e.model)
        if e.type in ("download", "print", "boost", "like", "collect") and key in seen:
            first = seen[key]
            first.count += e.count
            first.total = max(first.total, e.total)
        elif e.type in ("follower", "points") and (e.type, "") in seen:
            first = seen[(e.type, "")]
            first.count += e.count
            first.total = e.total
        else:
            e = Event(**{k: getattr(e, k) for k in e.__dataclass_fields__})
            seen[key] = e
            out.append(e)
    return out


def build_digest(events: list[Event], handle: str = "", heading: str = "") -> Digest:
    events = merge(events)
    order = {t: i for i, t in enumerate(EVENT_TYPES)}
    events = sorted(events, key=lambda e: (order.get(e.type, 99), -e.count))
    sections: list[Section] = []
    totals: dict[str, float] = {}
    for e in events:
        st = STYLES.get(e.type, HEALTH_WARN)
        if e.type == "health":
            st = HEALTH_OK if e.level == "ok" else HEALTH_WARN
            sections.append(Section(e.type, st, f"{st.emoji} {e.title}", [e.text] if e.text else []))
            continue
        totals[e.type] = totals.get(e.type, 0) + e.count
        if e.type == "comment":
            sections.append(Section(e.type, st, f"{st.emoji} New comment on {e.model}", [e.text],
                                    url=e.url, author=e.user))
        elif e.type == "follower":
            word = "New follower" if e.count == 1 else f"{signed(e.count)} {st.label(e.count)}"
            if e.count < 0:
                word = f"Lost {num(-e.count)} {st.label(e.count)}"
            sections.append(Section(e.type, st, f"{st.emoji} {word}", [f"{num(e.total)} followers total"],
                                    url=f"{MAKERWORLD}/@{handle}" if handle else ""))
        elif e.type == "points":
            sections.append(Section(e.type, st, f"{st.emoji} {signed(e.count)} {st.label(e.count)}",
                                    [f"{num(e.total)} points total"]))
        else:
            sec = next((s for s in sections if s.type == e.type), None)
            if sec is None:
                sec = Section(e.type, st, "")
                sections.append(sec)
            name = f"[{e.model}]({e.url})" if e.url else (e.model or "All models")
            sec.lines.append(f"{name}: {signed(e.count)}" + (f" ({num(e.total)} total)" if e.total else ""))
    for sec in sections:
        if sec.type in ("download", "print", "boost", "like", "collect"):
            n = totals[sec.type]
            sec.title = f"{sec.style.emoji} {signed(n)} {sec.style.label(n)}"
    parts = []
    for e in events:
        if e.type == "health" and e.title not in parts:
            parts.append(e.title)
    for t in EVENT_TYPES:
        if t in totals and t != "health":
            n = totals[t]
            st = STYLES[t]
            parts.append(f"{num(n)} new {st.label(n)}" if t == "comment" else f"{signed(n)} {st.label(n)}")
    summary = ", ".join(parts)
    if heading:
        summary = f"{heading}: {summary}" if summary else f"{heading}: no new activity"
        if not sections:
            sections.append(Section("digest", QUIET, f"{QUIET.emoji} No new activity overnight"))
    return Digest(summary, sections, events, heading)


def plain(line: str) -> str:
    """Markdown links -> plain text, for channels without markdown."""
    return re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", line)


def render_text(digest: Digest, markdown: bool = False) -> str:
    """The whole digest as text: markdown (links, bold, quotes) or plain."""
    blocks = []
    for s in digest.sections:
        head = f"**{s.title}**" if markdown else s.title
        if s.type == "comment":
            body = [f"{s.author}: " + (f"_{s.lines[0]}_" if markdown else f"“{s.lines[0]}”")]
        else:
            body = s.lines if markdown else [plain(ln) for ln in s.lines]
        if markdown and s.url and s.type in ("comment", "follower"):
            head = f"**[{s.title}]({s.url})**"
        blocks.append("\n".join([head, *body]))
    return "\n\n".join(blocks)
