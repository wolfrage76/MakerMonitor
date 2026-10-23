# MakerMonitor poller

Always-on Python agent:

1. Accepts MakerWorld cookies from the Chrome extension (`POST /auth/cookie`)
2. Polls MakerWorld for creator stats
3. Diffs totals and pushes `/sync` / `/notify` / `/status` to the CrowPanel

## Setup

```bash
cd poller
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp config.example.yaml config.yaml
# edit handle, panel_host, api_token
python -m makermonitor
```

Open `http://<poller-host>:8787/health` to verify.

## Color panel (optional)

Set `color_panel_host` in `config.yaml` to also drive color panels (see [`../firmware-color/`](../firmware-color/)); list several separated by commas. The poller then:

- keeps a day baseline + 30 days of daily totals in `data/daily.json` ("today" = since midnight),
- pushes `POST /v2/state` every poll and `POST /v2/event` per new boost / like / collect / download / print / comment,
- reads the panel's `GET /v2/settings` every 15 s, so the poll interval and "poll now" can be set from the panel's knob.

The e-ink panel is unchanged.

## Endpoints (for the extension)

| Path | Method | Auth | Purpose |
|------|--------|------|---------|
| `/health` | GET | no | Liveness |
| `/status` | GET | token | Cookie age, last poll, last error |
| `/auth/cookie` | POST | token | Update MakerWorld cookie jar |
| `/poll/now` | POST | token | Force an immediate poll |

Send header `Authorization: Bearer <api_token>` (or `X-Api-Token`).

### Cookie body

```json
{
  "cookie": "token=...; cf_clearance=...; ...",
  "source": "extension"
}
```

You can also send `cookies` as a name/value map; the poller will join them.

## Where to run it

Run on an **always-on Linux machine on your home LAN** — the same network (ideally same public IP) as the browser that mints Cloudflare `cf_clearance`. A remote VPS often fails those checks.

Requirements: Python 3.10+, outbound HTTPS to MakerWorld, inbound HTTP from your browser (extension) on the poller port (default `8787`), and LAN reachability to the CrowPanel.

## How stats are fetched

Unofficial, best-effort order:

1. Public profile page `https://makerworld.com/en/@{handle}` → parse `__NEXT_DATA__` for lifetime downloads / prints / boosts and model cards when present
2. With a valid cookie: Creator Center `data-overview` buildId + `_next/data/.../model.json` for points summary
3. With a valid cookie: `GET /api/v1/point-service/point-bill/my` (best-effort points balance)

If MakerWorld changes HTML/JSON shapes, update `makermonitor/makerworld.py`.

## Linux systemd install

Example layout: `/opt/makermonitor/poller` owned by a dedicated user.

```bash
sudo useradd --system --home /opt/makermonitor --shell /usr/sbin/nologin makermonitor
sudo mkdir -p /opt/makermonitor
sudo cp -a /path/to/MakerMonitor/poller /opt/makermonitor/
sudo chown -R makermonitor:makermonitor /opt/makermonitor

sudo -u makermonitor bash -lc '
  cd /opt/makermonitor/poller
  python3 -m venv .venv
  .venv/bin/pip install -r requirements.txt
  cp config.example.yaml config.yaml
  mkdir -p data
'
sudo -u makermonitor nano /opt/makermonitor/poller/config.yaml   # handle, panel_host, api_token

# Allow LAN clients to reach the poller (adjust zone/port if needed)
sudo ufw allow 8787/tcp   # only if you use ufw

sudo cp /opt/makermonitor/poller/makermonitor-poller.service /etc/systemd/system/
# If your install path or username differs, edit the unit first.
sudo systemctl daemon-reload
sudo systemctl enable --now makermonitor-poller
sudo systemctl status makermonitor-poller
journalctl -u makermonitor-poller -f
```

Unit file: [`makermonitor-poller.service`](makermonitor-poller.service). Change `User=`, `WorkingDirectory=`, and `ExecStart=` if you keep the repo in your home directory instead of `/opt`.

Point the Chrome extension at `http://<linux-host-lan-ip>:8787`.
