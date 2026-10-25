from __future__ import annotations

import time
from typing import Any

from .base import Channel, register
from .style import Digest, plain


@register
class Webhook(Channel):
    """Generic JSON POST. Config: url, optional headers (e.g. Authorization).

    Body: {"source", "handle", "time", "summary", "text", "events": [...]}; each
    event has type, count, total and, where they apply, model, model_id, url,
    user, text, level, title.
    """

    kind = "webhook"
    required = ("url",)

    def payload(self, digest: Digest) -> dict[str, Any]:
        text = "\n".join(
            "\n".join([s.title, *(plain(ln) for ln in s.lines)]) for s in digest.sections
        )
        return {"source": "makermonitor", "handle": self.handle, "time": int(time.time()),
                "summary": digest.summary, "text": text, "events": [e.to_dict() for e in digest.events]}

    async def send(self, digest: Digest) -> None:
        await self.post(self.cfg["url"], json=self.payload(digest), headers=self.cfg.get("headers") or {})
