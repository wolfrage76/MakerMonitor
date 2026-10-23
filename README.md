# MakerMonitor

Desk display for MakerWorld creator stats: an **Elecrow CrowPanel ESP32 2.13″ e-paper** and color panels (**Waveshare ESP32-S3 3.16″**, **Elecrow CrowPanel ESP32-S3 HMI 4.3″ / 5″ / 7″**). The 7″ color panel is the one currently in use.

No permanent MakerWorld browser tab. A thin Chrome extension refreshes session cookies when you visit MakerWorld; a Python poller keeps stats flowing to the panels.

```
You browse makerworld.com (occasionally)
        │
        ▼
Chrome extension  ──cookie update──►  Python poller (always on)
                                           │
                                           ├── HTTP /sync /notify ──►  e-paper panel
                                           └── HTTP /v2/state     ──►  color panel
```

## Pieces

| Path | Role |
|------|------|
| [`firmware-eink/`](firmware-eink/) | CrowPanel e-paper: WiFiManager + HTTP API + e-paper UI |
| [`firmware-color/`](firmware-color/) | Color panels (Waveshare 3.16" with knob, Elecrow CrowPanel 4.3/5/7" with touch): today's stats, live banners, web console |
| [`poller/`](poller/) | Background agent: cookie store, MakerWorld fetch, panel push |
| [`extension/`](extension/) | MV3 cookie courier + status popup |

## Quick start

1. **Flash a panel** — e-paper: [`firmware-eink/README.md`](firmware-eink/README.md). Color: [`firmware-color/README.md`](firmware-color/README.md). Note the IP on the display.
2. **Run the poller** — see [`poller/README.md`](poller/README.md). Copy `config.example.yaml` to `config.yaml`, then set your `@handle`, panel address, and `api_token`.
3. **Load the extension** — see [`extension/README.md`](extension/README.md). Visit MakerWorld once so the cookie is delivered.
4. The e-paper panel leaves `WAITING SYNC` after the first successful poll. A color panel fills in on its Today screen. Its web console is `http://<panel-ip>/`.

`config.yaml`, `poller/data/` (cookies and stats), `extension/LOAD_ME.txt`, and `platformio_override.ini` are gitignored. Do not add them.

## What is tracked

- Downloads, prints, boosts, points (when cookie is valid)
- Event flash on the e-paper when totals change
- Auth / sync status line on the dashboard

## Limitations

- MakerWorld has **no official creator API**. Endpoints are unofficial and can break when the site redeploys.
- Cookies expire. If you stop visiting MakerWorld, the poller will eventually get `AUTH` until you browse again.
- Cloudflare `cf_clearance` is often **IP-bound** — run the poller on an always-on **Linux host on your home LAN** (same public IP as your browser), not a remote VPS. See [`poller/README.md`](poller/README.md) for systemd.
- Updates are poll-interval based (default 10 minutes), not instant push from MakerWorld.
- Per-model event names depend on what the poller can scrape; if only lifetime totals are available, events are generic (`+3 downloads`).


## License

MIT — see [LICENSE](LICENSE).
