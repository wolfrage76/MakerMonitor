from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml
from pydantic import BaseModel, Field


class Settings(BaseModel):
    handle: str = Field(..., description="MakerWorld handle without @")
    panel_host: str
    panel_port: int = 80
    # Optional color panels (Waveshare 3.16", Elecrow CrowPanel 4.3/5/7").
    # One host, or several separated by commas; "host:port" overrides the port.
    color_panel_host: str = ""
    color_panel_port: int = 80
    poll_interval_sec: int = 600
    page_cycle_sec: int = 30
    listen_host: str = "0.0.0.0"
    listen_port: int = 8787
    api_token: str = "change-me"
    data_dir: str = "data"
    user_agent: str = (
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36"
    )

    @property
    def data_path(self) -> Path:
        return Path(self.data_dir)

    @property
    def color_panel_bases(self) -> list[str]:
        bases = []
        for host in self.color_panel_host.split(","):
            host = host.strip()
            if host:
                bases.append(f"http://{host}" if ":" in host else f"http://{host}:{self.color_panel_port}")
        return bases

    @property
    def panel_base(self) -> str:
        return f"http://{self.panel_host}:{self.panel_port}"


def load_settings(path: str | Path | None = None) -> Settings:
    cfg_path = Path(path) if path else Path(__file__).resolve().parent.parent / "config.yaml"
    if not cfg_path.exists():
        example = cfg_path.with_name("config.example.yaml")
        raise FileNotFoundError(
            f"Missing {cfg_path}. Copy {example.name} to config.yaml and edit it."
        )
    raw: dict[str, Any] = yaml.safe_load(cfg_path.read_text()) or {}
    return Settings(**raw)
