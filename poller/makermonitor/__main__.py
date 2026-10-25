from __future__ import annotations

import argparse
import asyncio
import logging
import uvicorn

from .config import config_path, load_settings
from .notify import NotifyHub
from .server import create_app


def main() -> None:
    parser = argparse.ArgumentParser(description="MakerMonitor poller")
    parser.add_argument(
        "-c",
        "--config",
        default=None,
        help="Path to config.yaml (default: ./config.yaml next to package parent)",
    )
    parser.add_argument(
        "--test-notify",
        action="store_true",
        help="Send a sample message to every notification channel and exit",
    )
    args = parser.parse_args()
    settings = load_settings(args.config)
    if args.test_notify:
        logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
        hub = NotifyHub(settings.notifications, settings.handle)
        if not hub.channels:
            raise SystemExit("No notification channels configured (see notifications: in config.example.yaml).")
        results = asyncio.run(hub.test())
        raise SystemExit(0 if all(results.values()) else 1)
    settings.data_path.mkdir(parents=True, exist_ok=True)
    app = create_app(settings, config_path(args.config))
    uvicorn.run(
        app,
        host=settings.listen_host,
        port=settings.listen_port,
        log_level="info",
    )


if __name__ == "__main__":
    main()