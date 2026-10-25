"""Which kind of panel is at each host.

Both firmwares answer GET /v2/settings with a "board" name ("CrowPanel 7.0",
"CrowPanel 2.13 e-ink"); e-paper firmware older than that only has GET
/stats. Results are cached in data/panels.json and checked again every few
hours, or sooner when a host stops or starts answering.
"""

from __future__ import annotations

import json
import logging
import time
from pathlib import Path
from typing import Any

import httpx

log = logging.getLogger(__name__)

RECHECK_SEC = 6 * 3600


async def detect(base: str, timeout: float = 4.0) -> dict[str, Any] | None:
    """{"type": "color" | "eink", "board", "fw"} or None when nothing answers."""
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            try:
                r = await client.get(f"{base}/v2/settings")
                if r.status_code == 200:
                    cfg = r.json()
                    board = str(cfg.get("board") or "")
                    eink = "e-ink" in board.lower() or "epaper" in board.lower() or "e-paper" in board.lower()
                    return {"type": "eink" if eink else "color", "board": board, "fw": str(cfg.get("fw") or "")}
            except ValueError:
                pass
            r = await client.get(f"{base}/stats")
            if r.status_code == 200:
                return {"type": "eink", "board": "e-paper", "fw": ""}
    except httpx.HTTPError:
        pass
    return None


class PanelDirectory:
    def __init__(self, data_dir: Path) -> None:
        self.path = data_dir / "panels.json"
        try:
            self.known: dict[str, dict[str, Any]] = json.loads(self.path.read_text())
        except (FileNotFoundError, json.JSONDecodeError):
            self.known = {}

    def _save(self) -> None:
        try:
            self.path.write_text(json.dumps(self.known))
        except OSError as exc:
            log.warning("panels: could not save %s: %s", self.path, exc)

    def kind(self, base: str) -> str:
        return (self.known.get(base) or {}).get("type", "unknown")

    async def check(self, base: str, force: bool = False) -> str:
        """Detect (or re-detect when stale or forced) one host; returns its type."""
        k = self.known.get(base)
        if k and not force and time.time() - k.get("t", 0) < RECHECK_SEC:
            return k["type"]
        found = await detect(base)
        if found:
            if not k or k.get("type") != found["type"]:
                log.info("panel %s: %s (%s %s)", base, found["type"], found["board"], found["fw"])
            self.known[base] = {**found, "t": time.time()}
            self._save()
            return found["type"]
        # Offline: keep what we knew; try again on the next check.
        return k["type"] if k else "unknown"

    def note_board(self, base: str, cfg: dict[str, Any]) -> bool:
        """A /v2/settings answer seen while polling. True when the type changed."""
        board = str(cfg.get("board") or "")
        kind = "eink" if "e-ink" in board.lower() else "color"
        k = self.known.get(base) or {}
        changed = k.get("type") != kind
        if changed or k.get("fw") != cfg.get("fw"):
            self.known[base] = {"type": kind, "board": board, "fw": str(cfg.get("fw") or ""),
                                "t": k.get("t", time.time()) if not changed else time.time()}
            self._save()
        return changed

    def view(self, bases: list[str], up: dict[str, bool]) -> list[dict[str, Any]]:
        out = []
        for b in bases:
            k = self.known.get(b) or {}
            out.append({"host": b.split("//", 1)[-1].removesuffix(":80"), "type": k.get("type", "unknown"),
                        "board": k.get("board", ""), "fw": k.get("fw", ""), "up": up.get(b)})
        return out
