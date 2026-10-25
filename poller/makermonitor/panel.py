from __future__ import annotations

import logging
from typing import Any

import httpx

from .pages import ascii_text
from .state import Snapshot

log = logging.getLogger(__name__)


class PanelClient:
    """E-ink panel. With no base URL (no panel configured) every call is a no-op."""

    def __init__(self, base_url: str, timeout: float = 10.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    async def ping(self) -> bool:
        if not self.base_url:
            return False
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                r = await client.get(f"{self.base_url}/ping")
                return r.status_code == 200 and r.text.strip() == "OK"
        except httpx.HTTPError as exc:
            log.warning("Panel ping failed: %s", exc)
            return False

    async def sync(self, snap: Snapshot) -> bool:
        body = {
            "accountName": snap.account_name,
            "totalDownloads": snap.downloads,
            "totalPrints": snap.prints,
            "totalBoosts": snap.boosts,
            "totalPoints": snap.points,
        }
        return await self._post("/sync", body)

    async def notify(self, payload: dict[str, Any]) -> bool:
        if "modelName" in payload:
            payload = {**payload, "modelName": ascii_text(payload["modelName"])}
        return await self._post("/notify", payload)

    async def settings(self) -> dict[str, Any] | None:
        """GET /v2/settings (firmware 0.3.0+): pollSec and the pollNow sequence."""
        if not self.base_url:
            return None
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                r = await client.get(f"{self.base_url}/v2/settings")
                if r.status_code == 200:
                    return r.json()
        except (httpx.HTTPError, ValueError):
            pass
        return None

    async def pages(self, payload: dict[str, Any]) -> bool:
        return await self._post("/pages", payload)

    async def status(self, auth: str, message: str, last_poll_age_sec: int | None = None) -> bool:
        body: dict[str, Any] = {"auth": auth, "message": ascii_text(message)}
        if last_poll_age_sec is not None:
            body["lastPollAgeSec"] = last_poll_age_sec
        return await self._post("/status", body)

    async def _post(self, path: str, body: dict[str, Any]) -> bool:
        if not self.base_url:
            return False
        try:
            async with httpx.AsyncClient(timeout=self.timeout) as client:
                r = await client.post(f"{self.base_url}{path}", json=body)
                if r.status_code >= 400:
                    log.warning("Panel %s -> %s %s", path, r.status_code, r.text[:200])
                    return False
                return True
        except httpx.HTTPError as exc:
            log.warning("Panel %s failed: %s", path, exc)
            return False


def build_events(prev: Snapshot, cur: Snapshot) -> list[dict[str, Any]]:
    """Turn a snapshot diff into notify payloads (compatible with original monitor)."""
    events: list[dict[str, Any]] = []
    totals = {
        "currentTotalDownloads": cur.downloads,
        "currentTotalPrints": cur.prints,
        "currentTotalBoosts": cur.boosts,
        "currentTotalPoints": cur.points,
    }

    # Prefer per-model diffs when both sides have model maps.
    if prev.models and cur.models:
        for mid, m in cur.models.items():
            old = prev.models.get(mid)
            if not old:
                continue
            if m.downloads > old.downloads:
                events.append(
                    {
                        "type": "download",
                        "modelName": m.name or mid,
                        "value": m.downloads - old.downloads,
                        "total": m.downloads,
                        "points": float(m.downloads - old.downloads),
                        **totals,
                    }
                )
            if m.prints > old.prints:
                events.append(
                    {
                        "type": "print",
                        "modelName": m.name or mid,
                        "value": m.prints - old.prints,
                        "total": m.prints,
                        "points": float((m.prints - old.prints) * 2),
                        **totals,
                    }
                )
            if m.boosts > old.boosts:
                events.append(
                    {
                        "type": "boost",
                        "modelName": m.name or mid,
                        "value": m.boosts - old.boosts,
                        "total": m.boosts,
                        "points": 0,
                        **totals,
                    }
                )
            if m.likes > old.likes:
                events.append(
                    {
                        "type": "like",
                        "modelName": m.name or mid,
                        "value": m.likes - old.likes,
                        "total": m.likes,
                        "points": 0,
                        **totals,
                    }
                )

    # Comments: only ones newer than anything seen before (skips own replies).
    prev_comments = prev.extras.get("comments") or []
    seen = {c.get("id") for c in prev_comments}
    newest_seen = max((c.get("time") or "" for c in prev_comments), default="")
    if "comments" in prev.extras:
        for c in reversed(cur.extras.get("comments") or []):
            if c.get("id") in seen or (c.get("time") or "") <= newest_seen:
                continue
            model = cur.models.get(str(c.get("designId")))
            events.append(
                {
                    "type": "comment",
                    "modelName": c.get("model") or str(c.get("designId")),
                    "value": 1,
                    "total": model.comments if model else 0,
                    "points": 0,
                    "user": ascii_text(c.get("user", "")),
                    "text": ascii_text(c.get("text", ""))[:120],
                    **totals,
                }
            )

    # Fall back to aggregate deltas if no per-model events.
    if not events:
        if cur.downloads > prev.downloads:
            events.append(
                {
                    "type": "download",
                    "modelName": "Models",
                    "value": cur.downloads - prev.downloads,
                    "total": cur.downloads,
                    "points": float(cur.downloads - prev.downloads),
                    **totals,
                }
            )
        if cur.prints > prev.prints:
            events.append(
                {
                    "type": "print",
                    "modelName": "Models",
                    "value": cur.prints - prev.prints,
                    "total": cur.prints,
                    "points": float((cur.prints - prev.prints) * 2),
                    **totals,
                }
            )
        if cur.boosts > prev.boosts:
            events.append(
                {
                    "type": "boost",
                    "modelName": "Models",
                    "value": cur.boosts - prev.boosts,
                    "total": cur.boosts,
                    "points": 0,
                    **totals,
                }
            )

    if cur.points > prev.points + 0.049:
        events.append(
            {
                "type": "points",
                "before": prev.points,
                "after": cur.points,
                "increase": round(cur.points - prev.points, 2),
                **totals,
            }
        )

    return events


class PanelGroup:
    """Every e-paper panel behind the same calls as one PanelClient."""

    def __init__(self, clients: list[PanelClient]) -> None:
        self.clients = clients

    @property
    def base_url(self) -> str:
        return ", ".join(c.base_url for c in self.clients)

    async def ping(self) -> bool:
        return any([await c.ping() for c in self.clients])

    async def sync(self, snap: Snapshot) -> bool:
        return bool(self.clients) and all([await c.sync(snap) for c in self.clients])

    async def notify(self, payload: dict[str, Any]) -> bool:
        return bool(self.clients) and all([await c.notify(payload) for c in self.clients])

    async def pages(self, payload: dict[str, Any]) -> bool:
        return bool(self.clients) and all([await c.pages(payload) for c in self.clients])

    async def status(self, auth: str, message: str, last_poll_age_sec: int | None = None) -> bool:
        return bool(self.clients) and all([await c.status(auth, message, last_poll_age_sec) for c in self.clients])

