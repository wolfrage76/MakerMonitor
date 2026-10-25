from __future__ import annotations

from typing import Any

from .base import Channel, register
from .style import Digest, plain

API = "https://api.pushbullet.com/v2/pushes"


@register
class Pushbullet(Channel):
    """Pushbullet note push. Config: token (Access Token), optional device (device_iden) or channel (tag)."""

    kind = "pushbullet"
    required = ("token",)

    def payload(self, digest: Digest) -> dict[str, Any]:
        lines = []
        for s in digest.sections:
            if s.type == "comment":
                lines.append(f"{s.title}\n{s.author}: “{s.lines[0]}”")
            else:
                lines.append("\n".join([s.title, *(f"  {plain(ln)}" for ln in s.lines)]))
        p: dict[str, Any] = {"type": "note", "title": f"MakerMonitor: {digest.summary}"[:250],
                             "body": "\n\n".join(lines)[:4000]}
        if self.cfg.get("device"):
            p["device_iden"] = self.cfg["device"]
        elif self.cfg.get("channel"):
            p["channel_tag"] = self.cfg["channel"]
        return p

    async def send(self, digest: Digest) -> None:
        await self.post(API, json=self.payload(digest), headers={"Access-Token": self.cfg["token"]})
