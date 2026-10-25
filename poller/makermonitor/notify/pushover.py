from __future__ import annotations

from html import escape
from typing import Any

from .base import Channel, register
from .style import HEALTH_OK, Digest, plain

API = "https://api.pushover.net/1/messages.json"


@register
class Pushover(Channel):
    """Pushover. Config: token (application API token), user (user or group key),
    optional device, sound, priority (-2..1, default 0), alert_priority (health
    warnings, default 1 = bypasses the phone's quiet hours) and alert_sound."""

    kind = "pushover"
    required = ("token", "user")

    def payload(self, digest: Digest) -> dict[str, Any]:
        blocks = []
        for s in digest.sections:
            head = f"<b>{escape(s.title)}</b>"
            if s.type == "comment":
                blocks.append(f"{head}\n{escape(s.author)}: <i>{escape(s.lines[0])}</i>")
            else:
                blocks.append("\n".join([head, *(escape(plain(ln)) for ln in s.lines)]))
        alert = any(s.type == "health" and s.style is not HEALTH_OK for s in digest.sections)
        p: dict[str, Any] = {
            "token": self.cfg["token"], "user": self.cfg["user"], "html": 1,
            "title": f"MakerMonitor: {digest.summary}"[:250],
            "message": _fit(blocks),
            "priority": int(self.cfg.get("alert_priority", 1) if alert else self.cfg.get("priority", 0)),
        }
        url = next((s.url for s in digest.sections if s.url), "")
        if url:
            p.update(url=url, url_title="Open on MakerWorld")
        sound = self.cfg.get("alert_sound") if alert else None
        if sound or self.cfg.get("sound"):
            p["sound"] = sound or self.cfg["sound"]
        if self.cfg.get("device"):
            p["device"] = self.cfg["device"]
        return p

    async def send(self, digest: Digest) -> None:
        await self.post(API, data=self.payload(digest))


def _fit(blocks: list[str], limit: int = 1024) -> str:
    """Whole blocks only, so the HTML is never cut mid-tag (Pushover's limit is 1024)."""
    out = ""
    for i, b in enumerate(blocks):
        more = f"\n\n(+{len(blocks) - i} more)"
        if len(out) + len(b) + 2 + len(more) > limit:
            return (out + more).strip() if out else b[:limit]
        out += ("\n\n" if out else "") + b
    return out
