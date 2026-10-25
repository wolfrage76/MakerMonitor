# MakerMonitor

Desk display and notifier for MakerWorld creator stats: an **Elecrow CrowPanel ESP32 2.13″ e-paper** and color panels (**Waveshare ESP32-S3 3.16″**, **Elecrow CrowPanel ESP32-S3 HMI 4.3″ / 5″ / 7″**). The 7″ color panel is the one currently in use.

No permanent MakerWorld browser tab. A thin Chrome extension refreshes session cookies when you visit MakerWorld; a Python poller keeps stats flowing to the panels and, optionally, to Discord, Pushover, ntfy, Telegram, Home Assistant and other notification services.

```
You browse makerworld.com (occasionally)
        │
        ▼
Chrome extension  ──cookie update──►  Python poller (always on)
                                           │
                                           ├── HTTP /sync /notify ──►  e-paper panel
                                           ├── HTTP /v2/state     ──►  color panel
                                           └── notifications      ──►  Discord, Pushover, ntfy, Telegram, ...
```

## Pieces

| Path | Role |
|------|------|
| [`firmware-eink/`](firmware-eink/) | CrowPanel e-paper: WiFiManager + HTTP API + e-paper UI |
| [`firmware-color/`](firmware-color/) | Color panels (Waveshare 3.16" with knob, Elecrow CrowPanel 4.3/5/7" with touch): today's stats, 7/30-day trends, top models with per-model detail, best times, creator stats and followers, live banners, quiet hours, on-screen Wi-Fi setup, web console, screen-on firmware updates (7"), screenshot and page-switch API for testing |
| [`poller/`](poller/) | Background agent: cookie store, MakerWorld fetch, panel push, notifications ([`NOTIFICATIONS.md`](poller/NOTIFICATIONS.md)) |
| [`extension/`](extension/) | MV3 cookie courier + status popup |

## Quick start

1. **Flash a panel** — e-paper: [`firmware-eink/README.md`](firmware-eink/README.md). Color: [`firmware-color/README.md`](firmware-color/README.md). Note the IP on the display.
2. **Run the poller** — see [`poller/README.md`](poller/README.md). Copy `config.example.yaml` to `config.yaml`, then set your `@handle`, panel address, and `api_token`. Panels are optional: the poller can run with notifications only.
3. **Load the extension** — see [`extension/README.md`](extension/README.md). Visit MakerWorld once so the cookie is delivered.
4. Optional: add notification channels under `notifications:` and test them with `.venv/bin/python -m makermonitor --test-notify` (see [`poller/NOTIFICATIONS.md`](poller/NOTIFICATIONS.md)).
5. The e-paper panel leaves `WAITING SYNC` after the first successful poll. A color panel fills in on its Today screen (until then it shows a **Connect the poller** page with the `color_panel_host` line to set). Its web console is `http://<panel-ip>/` or `http://<name>.local/`.

The poller also serves a LAN dashboard at `http://<poller-host>:8787/?token=<api_token>` with stats, a sortable model table, health and a Settings view that edits `config.yaml` and applies it live (see [`poller/README.md`](poller/README.md#dashboard)). It can also run in Docker ([`poller/README.md`](poller/README.md#docker)).

`config.yaml`, `poller/data/` (cookies and stats), `extension/LOAD_ME.txt`, and `platformio_override.ini` are gitignored. Do not add them.

## What is tracked

- Downloads, prints, boosts, likes, collects, comments, followers, points (points and the 30-day reach report need a valid cookie)
- Today (since midnight), yesterday, 7 and 30 days, per-model daily history, and best hours and weekdays
- Creator stats from MakerWorld's own per-model analytics: downloads per view, prints per download, engagement per 100 downloads, momentum (rising and stalled models), launch curves against your typical launch, points and followers per model, long-tail share and unanswered comments (color panels and the dashboard)
- Event banners and alerts when counts change
- Poller health (cookie ok / expired, data stale, per-source fetch status, MakerWorld request budget) on the panels and the dashboard
- Optional notifications per event type (downloads, prints, boosts, likes, collects, comments, followers, points, poller health), with per-channel event selection, quiet hours and a "while you were away" digest

## Limitations

- MakerWorld has **no official creator API**. Endpoints are unofficial and can break when the site redeploys.
- Cookies expire. If you stop visiting MakerWorld, the poller will eventually get `AUTH` until you browse again.
- Cloudflare `cf_clearance` is often **IP-bound** — run the poller on an always-on **Linux host on your home LAN** (same public IP as your browser), not a remote VPS. See [`poller/README.md`](poller/README.md) for systemd.
- Updates are poll-interval based (default 10 minutes), not instant push from MakerWorld.
- All MakerWorld traffic goes through one rate limiter (one request at a time, at least ~3 s apart, about 120 requests an hour, with backoff on errors). Creator stats are fetched a few models at a time in the background, so they take a few hours to fill in after the first start.
- The e-paper panel is on hold: it gets the TODAY / 30 days / top models / best times / comments / account pages, but not the creator stats.
- Per-model event names depend on what the poller can scrape; if only lifetime totals are available, events are generic (`+3 downloads`).


## License

MIT — see [LICENSE](LICENSE).
