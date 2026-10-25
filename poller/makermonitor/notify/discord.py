from __future__ import annotations

from typing import Any

from .base import Channel, register
from .style import HEALTH_OK, Digest

_MAX_EMBEDS = 10  # Discord's limit per message


@register
class Discord(Channel):
    """Discord webhook. Config: url, optional username, avatar_url, mention (e.g. "<@123>" for alerts)."""

    kind = "discord"
    required = ("url",)

    def payloads(self, digest: Digest) -> list[dict[str, Any]]:
        embeds = []
        for s in digest.sections:
            e: dict[str, Any] = {"title": s.title[:256], "color": s.style.color}
            if s.lines:
                desc = "\n".join(f"> {s.lines[0]}" if s.type == "comment" else ln for ln in s.lines)
                e["description"] = desc[:4000]
            if s.url:
                e["url"] = s.url
            if s.author:
                e["author"] = {"name": s.author[:256]}
            embeds.append(e)
        base: dict[str, Any] = {"username": self.cfg.get("username") or "MakerMonitor"}
        if self.cfg.get("avatar_url"):
            base["avatar_url"] = self.cfg["avatar_url"]
        out = []
        for i in range(0, len(embeds), _MAX_EMBEDS):
            p = {**base, "embeds": embeds[i:i + _MAX_EMBEDS]}
            if i == 0:
                content = [f"🌙 **{digest.heading}**"] if digest.heading else []
                if self.cfg.get("mention") and any(s.type == "health" and s.style is not HEALTH_OK for s in digest.sections):
                    content.append(self.cfg["mention"])
                if content:
                    p["content"] = " ".join(content)
            out.append(p)
        return out

    async def send(self, digest: Digest) -> None:
        for p in self.payloads(digest):
            await self.post(self.cfg["url"], json=p)
