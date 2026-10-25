from __future__ import annotations

import re
from html import escape
from typing import Any

from .base import Channel, register
from .style import Digest

API = "https://api.telegram.org"
_LIMIT = 4096


@register
class Telegram(Channel):
    """Telegram bot. Config: token (from @BotFather), chat_id (your user id, a
    group id, or @channelname), optional silent: true (no sound)."""

    kind = "telegram"
    required = ("token", "chat_id")

    def text(self, digest: Digest) -> str:
        blocks = []
        for s in digest.sections:
            title = escape(s.title)
            head = f'<b><a href="{escape(s.url)}">{title}</a></b>' if s.url else f"<b>{title}</b>"
            if s.type == "comment":
                body = [f"{escape(s.author)}:", f"<blockquote>{escape(s.lines[0])}</blockquote>"]
            else:
                body = [_links(ln) for ln in s.lines]
            blocks.append("\n".join([head, *body]))
        out = ""
        for i, b in enumerate(blocks):  # whole blocks only, so tags are never cut
            if len(out) + len(b) + 20 > _LIMIT:
                return out + f"\n\n(+{len(blocks) - i} more)"
            out += ("\n\n" if out else "") + b
        return out

    def payload(self, digest: Digest) -> dict[str, Any]:
        return {"chat_id": self.cfg["chat_id"], "text": self.text(digest), "parse_mode": "HTML",
                "disable_web_page_preview": True, "disable_notification": bool(self.cfg.get("silent"))}

    async def send(self, digest: Digest) -> None:
        await self.post(f"{API}/bot{self.cfg['token']}/sendMessage", json=self.payload(digest))


def _links(line: str) -> str:
    """"[name](url): +3" -> HTML link, everything else escaped."""
    parts, last = [], 0
    for m in re.finditer(r"\[([^\]]+)\]\(([^)]+)\)", line):
        parts.append(escape(line[last:m.start()]))
        parts.append(f'<a href="{escape(m.group(2))}">{escape(m.group(1))}</a>')
        last = m.end()
    parts.append(escape(line[last:]))
    return "".join(parts)
