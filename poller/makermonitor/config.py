from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml
from pydantic import BaseModel, Field, field_validator


class Settings(BaseModel):
    handle: str = Field(..., description="MakerWorld handle without @")
    # Panels to push to: IPs or .local names ("host:port" for another port).
    # Color or e-paper is detected per host (panels.py).
    panels: list[str] = Field(default_factory=list)
    # Before `panels`: one e-ink host and comma-separated color hosts. Still
    # read and merged into the list; the log asks to move them.
    panel_host: str = ""
    panel_port: int = 80
    color_panel_host: str = ""
    color_panel_port: int = 80
    poll_interval_sec: int = 600
    page_cycle_sec: int = 30
    listen_host: str = "0.0.0.0"
    listen_port: int = 8787
    api_token: str = "change-me"
    data_dir: str = "data"
    # Optional push notifications; see config.example.yaml.
    notifications: dict[str, Any] = Field(default_factory=dict)
    user_agent: str = (
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"
    )

    @field_validator("panels", mode="before")
    @classmethod
    def _panels_list(cls, v: Any) -> list[str]:
        if v is None:
            return []
        if isinstance(v, str):
            v = v.split(",")
        return [str(h).strip() for h in v if str(h).strip()]

    @property
    def data_path(self) -> Path:
        return Path(self.data_dir)

    @property
    def legacy_panel_keys(self) -> list[str]:
        return [k for k in ("color_panel_host", "panel_host") if getattr(self, k).strip()]

    @property
    def panel_hosts(self) -> list[str]:
        """Every panel host, `panels` first, then the old keys, without repeats."""
        out: list[str] = []
        legacy = [(h, self.color_panel_port) for h in self.color_panel_host.split(",")]
        legacy.append((self.panel_host, self.panel_port))
        for h in self.panels + [f"{h.strip()}:{p}" if h.strip() and ":" not in h and p != 80 else h for h, p in legacy]:
            h = h.strip()
            if h and h not in out:
                out.append(h)
        return out

    @property
    def panel_bases(self) -> list[str]:
        return [f"http://{h}" if ":" in h else f"http://{h}:80" for h in self.panel_hosts]


def config_path(path: str | Path | None = None) -> Path:
    return Path(path) if path else Path(__file__).resolve().parent.parent / "config.yaml"


def load_settings(path: str | Path | None = None) -> Settings:
    cfg_path = config_path(path)
    if not cfg_path.exists():
        example = cfg_path.with_name("config.example.yaml")
        raise FileNotFoundError(
            f"Missing {cfg_path}. Copy {example.name} to config.yaml and edit it."
        )
    raw: dict[str, Any] = yaml.safe_load(cfg_path.read_text()) or {}
    return Settings(**raw)
