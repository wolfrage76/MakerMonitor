from __future__ import annotations

from typing import Any

from .base import Channel, register
from .style import HEALTH_OK, Digest, render_text

# ntfy tags that render as emoji next to the title.
_TAGS = {"download": "arrow_down", "print": "printer", "boost": "rocket", "like": "heart",
         "collect": "pushpin", "comment": "speech_balloon", "follower": "bust_in_silhouette",
         "points": "star"}


@register
class Ntfy(Channel):
    """ntfy.sh or a self-hosted ntfy. Config: topic, optional server (default
    https://ntfy.sh), token or username/password, priority (1-5, default 3),
    alert_priority (health warnings, default 5)."""

    kind = "ntfy"
    required = ("topic",)

    def payload(self, digest: Digest) -> dict[str, Any]:
        alert = any(s.type == "health" and s.style is not HEALTH_OK for s in digest.sections)
        ok = any(s.style is HEALTH_OK for s in digest.sections)
        tags = ["warning"] if alert else ["white_check_mark"] if ok else []
        tags += [_TAGS[t] for t in dict.fromkeys(s.type for s in digest.sections) if t in _TAGS]
        text = render_text(digest, markdown=True)
        p: dict[str, Any] = {
            "topic": self.cfg["topic"], "title": f"MakerMonitor: {digest.summary}"[:250],
            "message": text[:4000], "markdown": True, "tags": tags[:5],
            "priority": int(self.cfg.get("alert_priority", 5) if alert else self.cfg.get("priority", 3)),
        }
        url = next((s.url for s in digest.sections if s.url), "")
        if url:
            p["click"] = url
        return p

    async def send(self, digest: Digest) -> None:
        server = str(self.cfg.get("server") or "https://ntfy.sh").rstrip("/")
        kw: dict[str, Any] = {"json": self.payload(digest)}
        if self.cfg.get("token"):
            kw["headers"] = {"Authorization": f"Bearer {self.cfg['token']}"}
        elif self.cfg.get("username"):
            kw["auth"] = (self.cfg["username"], str(self.cfg.get("password") or ""))
        await self.post(server, **kw)
