from __future__ import annotations

from typing import Any

from .base import Channel, register
from .style import HEALTH_OK, Digest, render_text


@register
class HomeAssistant(Channel):
    """Home Assistant. Config: url (e.g. http://homeassistant.local:8123), token
    (Profile > Security > Long-lived access tokens), and one or both of:
      service  notify service, e.g. mobile_app_pixel_8 (pushes to the HA app)
      event    event type fired for automations, e.g. makermonitor (data = digest)
    """

    kind = "homeassistant"
    required = ("url", "token")

    def __init__(self, *a: Any, **kw: Any) -> None:
        super().__init__(*a, **kw)
        if not (self.cfg.get("service") or self.cfg.get("event")):
            raise ValueError("set service and/or event")

    def notify_payload(self, digest: Digest) -> dict[str, Any]:
        alert = any(s.type == "health" and s.style is not HEALTH_OK for s in digest.sections)
        p: dict[str, Any] = {"title": f"MakerMonitor: {digest.summary}"[:250],
                             "message": render_text(digest)[:2000]}
        url = next((s.url for s in digest.sections if s.url), "")
        data: dict[str, Any] = {"group": "makermonitor"}
        if url:
            data.update(url=url, clickAction=url)  # iOS / Android companion app
        if alert:
            data.update(priority="high", push={"interruption-level": "time-sensitive"})
        p["data"] = data
        return p

    def event_payload(self, digest: Digest) -> dict[str, Any]:
        return {"summary": digest.summary, "handle": self.handle,
                "types": sorted({e.type for e in digest.events}),
                "events": [e.to_dict() for e in digest.events]}

    async def send(self, digest: Digest) -> None:
        base = str(self.cfg["url"]).rstrip("/")
        headers = {"Authorization": f"Bearer {self.cfg['token']}"}
        if self.cfg.get("service"):
            svc = str(self.cfg["service"]).removeprefix("notify.")
            await self.post(f"{base}/api/services/notify/{svc}", json=self.notify_payload(digest), headers=headers)
        if self.cfg.get("event"):
            await self.post(f"{base}/api/events/{self.cfg['event']}", json=self.event_payload(digest), headers=headers)
