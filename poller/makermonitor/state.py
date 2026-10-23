from __future__ import annotations

import json
import threading
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class ModelStats:
    name: str = ""
    downloads: int = 0
    prints: int = 0
    boosts: int = 0
    likes: int = 0
    comments: int = 0
    collects: int = 0


@dataclass
class Snapshot:
    account_name: str = ""
    downloads: int = 0
    prints: int = 0
    boosts: int = 0
    points: float = 0.0
    models: dict[str, ModelStats] = field(default_factory=dict)
    fetched_at: float = 0.0
    source: str = ""
    # Display-only data: account, month, comments (not used for diffs)
    extras: dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        return {
            "account_name": self.account_name,
            "downloads": self.downloads,
            "prints": self.prints,
            "boosts": self.boosts,
            "points": self.points,
            "models": {k: asdict(v) for k, v in self.models.items()},
            "fetched_at": self.fetched_at,
            "source": self.source,
            "extras": self.extras,
        }

    @classmethod
    def from_dict(cls, data: dict[str, Any] | None) -> Snapshot:
        if not data:
            return cls()
        models = {
            k: ModelStats(**v) if isinstance(v, dict) else ModelStats()
            for k, v in (data.get("models") or {}).items()
        }
        return cls(
            account_name=data.get("account_name") or "",
            downloads=int(data.get("downloads") or 0),
            prints=int(data.get("prints") or 0),
            boosts=int(data.get("boosts") or 0),
            points=float(data.get("points") or 0.0),
            models=models,
            fetched_at=float(data.get("fetched_at") or 0.0),
            source=data.get("source") or "",
            extras=data.get("extras") or {},
        )


@dataclass
class RuntimeStatus:
    cookie_present: bool = False
    cookie_updated_at: float = 0.0
    last_poll_at: float = 0.0
    last_poll_ok: bool = False
    last_error: str = ""
    auth_ok: bool = False
    last_snapshot: Snapshot = field(default_factory=Snapshot)
    panel_reachable: bool = False


class Store:
    """Thread-safe persistence for cookie + last snapshot."""

    def __init__(self, data_dir: Path) -> None:
        self.data_dir = data_dir
        self.data_dir.mkdir(parents=True, exist_ok=True)
        self._lock = threading.RLock()
        self.cookie: str = ""
        self.status = RuntimeStatus()
        self._load()

    @property
    def cookie_path(self) -> Path:
        return self.data_dir / "cookie.txt"

    @property
    def snapshot_path(self) -> Path:
        return self.data_dir / "snapshot.json"

    def _load(self) -> None:
        if self.cookie_path.exists():
            self.cookie = self.cookie_path.read_text().strip()
            self.status.cookie_present = bool(self.cookie)
            self.status.cookie_updated_at = self.cookie_path.stat().st_mtime
        if self.snapshot_path.exists():
            try:
                data = json.loads(self.snapshot_path.read_text())
                self.status.last_snapshot = Snapshot.from_dict(data)
            except json.JSONDecodeError:
                pass

    def set_cookie(self, cookie: str) -> bool:
        """Returns True if cookie content changed."""
        cookie = cookie.strip()
        with self._lock:
            changed = cookie != self.cookie
            if not cookie:
                return False
            self.cookie = cookie
            self.cookie_path.write_text(cookie)
            self.cookie_path.chmod(0o600)
            self.status.cookie_present = True
            self.status.cookie_updated_at = time.time()
            return changed

    def get_cookie(self) -> str:
        with self._lock:
            return self.cookie

    def save_snapshot(self, snap: Snapshot) -> None:
        with self._lock:
            self.status.last_snapshot = snap
            self.snapshot_path.write_text(json.dumps(snap.to_dict(), indent=2))

    @property
    def history_path(self) -> Path:
        return self.data_dir / "history.json"

    def record_history(self, snap: Snapshot, window_sec: float = 86400) -> dict[str, Any]:
        """Append counters for this poll; return the entry closest to window_sec ago.

        Keeps only the newest entry older than the window plus everything newer,
        so the file stays small. If history is shorter than the window, the
        oldest entry is returned (caller shows it as "since ...").
        """
        acct = snap.extras.get("account") or {}
        entry = {
            "t": snap.fetched_at or time.time(),
            "downloads": snap.downloads,
            "prints": snap.prints,
            "boosts": snap.boosts,
            "points": snap.points,
            "likes": acct.get("likes", 0),
            "saves": acct.get("saves", 0),
            "followers": acct.get("followers", 0),
        }
        with self._lock:
            try:
                hist = json.loads(self.history_path.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                hist = []
            hist.append(entry)
            cutoff = entry["t"] - window_sec
            older = [h for h in hist if h["t"] <= cutoff]
            hist = older[-1:] + [h for h in hist if h["t"] > cutoff]
            self.history_path.write_text(json.dumps(hist))
            return hist[0]

    def public_status(self) -> dict[str, Any]:
        with self._lock:
            age = None
            if self.status.cookie_updated_at:
                age = int(time.time() - self.status.cookie_updated_at)
            poll_age = None
            if self.status.last_poll_at:
                poll_age = int(time.time() - self.status.last_poll_at)
            snap = self.status.last_snapshot
            return {
                "cookie_present": self.status.cookie_present,
                "cookie_age_sec": age,
                "auth_ok": self.status.auth_ok,
                "last_poll_at": self.status.last_poll_at,
                "last_poll_age_sec": poll_age,
                "last_poll_ok": self.status.last_poll_ok,
                "last_error": self.status.last_error,
                "panel_reachable": self.status.panel_reachable,
                "totals": {
                    "account_name": snap.account_name,
                    "downloads": snap.downloads,
                    "prints": snap.prints,
                    "boosts": snap.boosts,
                    "points": snap.points,
                    "source": snap.source,
                },
            }
