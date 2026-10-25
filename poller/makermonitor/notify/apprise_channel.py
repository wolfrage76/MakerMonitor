from __future__ import annotations

from typing import Any

from .base import Channel, register
from .style import HEALTH_OK, Digest, render_text


@register
class Apprise(Channel):
    """Any service Apprise supports (Slack, Matrix, Gotify, Signal, Teams, ...),
    one URL each: https://github.com/caronc/apprise/wiki. Config: urls (list or
    one string). Needs the optional package: .venv/bin/python -m pip install apprise
    """

    kind = "apprise"
    required = ("urls",)

    def __init__(self, *a: Any, **kw: Any) -> None:
        super().__init__(*a, **kw)
        try:
            import apprise
        except ImportError:
            raise ValueError("the apprise package is not installed (pip install apprise)") from None
        urls = self.cfg["urls"]
        self.ap = apprise.Apprise()
        for u in [urls] if isinstance(urls, str) else urls:
            if not self.ap.add(u):
                raise ValueError(f"Apprise doesn't recognise one of the urls ({str(u).split(':', 1)[0]}://...)")

    async def send(self, digest: Digest) -> None:
        import apprise
        alert = any(s.type == "health" and s.style is not HEALTH_OK for s in digest.sections)
        ok = await self.ap.async_notify(
            body=render_text(digest, markdown=True), title=f"MakerMonitor: {digest.summary}"[:250],
            body_format=apprise.NotifyFormat.MARKDOWN,
            notify_type=apprise.NotifyType.WARNING if alert else apprise.NotifyType.INFO)
        if not ok:
            raise RuntimeError("Apprise reported a failed delivery (see apprise log lines above)")
