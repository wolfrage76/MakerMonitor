from __future__ import annotations

import argparse
import uvicorn

from .config import load_settings
from .server import create_app


def main() -> None:
    parser = argparse.ArgumentParser(description="MakerMonitor poller")
    parser.add_argument(
        "-c",
        "--config",
        default=None,
        help="Path to config.yaml (default: ./config.yaml next to package parent)",
    )
    args = parser.parse_args()
    settings = load_settings(args.config)
    settings.data_path.mkdir(parents=True, exist_ok=True)
    app = create_app(settings)
    uvicorn.run(
        app,
        host=settings.listen_host,
        port=settings.listen_port,
        log_level="info",
    )


if __name__ == "__main__":
    main()