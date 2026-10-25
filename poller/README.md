# MakerMonitor poller

Always-on Python agent:

1. Accepts MakerWorld cookies from the Chrome extension (`POST /auth/cookie`)
2. Polls MakerWorld for creator stats
3. Diffs totals and pushes `/sync` / `/notify` / `/status` / `/pages` to the e-paper CrowPanel and `/v2/state` / `/v2/event` to color panels (both optional)
4. Optionally sends notifications to Discord, Pushover, ntfy, Telegram and others ([`NOTIFICATIONS.md`](NOTIFICATIONS.md))
5. Serves a LAN dashboard at `http://<poller-host>:8787/` (see [Dashboard](#dashboard))

## Setup

```bash
cd poller
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp config.example.yaml config.yaml
# edit handle, api_token, and panel_host / color_panel_host / notifications as needed
python -m makermonitor
```

Open `http://<poller-host>:8787/health` to verify.

What happened overnight is in `data/poller.log` (rotated, 1 MB × 5). Each poll logs the cookie age, MakerWorld result, and whether the e-paper and color panels accepted the update. An extension cookie push is logged there too, by cookie name only.

## Color panel (optional)

Set `color_panel_host` in `config.yaml` to also drive color panels (see [`../firmware-color/`](../firmware-color/)); list several separated by commas. The poller then:

- keeps a day baseline, 30 days of daily totals, per-day hourly buckets (35 days) and per-model daily baselines (31 days) in `data/daily.json` ("today" = since midnight),
- pushes `POST /v2/state` every poll and `POST /v2/event` per new boost / like / collect / download / print / comment,
- reads every panel's `GET /v2/settings` (color and e-paper) every 15 s, so the poll interval and "poll now" can be set from any panel.

The state also carries:

- `insights`: activity by hour of day (24) and weekday (7) over the last 30 days for downloads, prints and boosts, with `bestHour` / `bestDay`, `days` collected and `has` (at least 7 days). Panels show "collecting data" until then.
- `health`: `auth` (`ok`, `expired`, `none`), `stale`, `since`, and per source (`models`, `reach` = the 30-day data-overview page, `points`) the last good fetch time, consecutive failures and the last error. **`expired`** means MakerWorld rejected the cookie (401/403) for models or points; the data-overview page is sometimes refused on its own, which counts as a reach error, not an expired cookie. **`stale`** means the last good models fetch is older than 3× the poll interval.
- `health.src["model-stats"]`: the background per-model analytics fetch (see [Creator stats](#creator-stats)), tracked on its own so it never changes `auth` or `stale`.
- `health.limiter`: `requests_last_hour`, `budget`, `by_kind` (core / background), `backoff_until` per endpoint, `paused`, and any rate-limit headers MakerWorld sent (see [MakerWorld requests](#makerworld-requests)).
- `creator`: the compact creator stats (top 4 per list), see below.
- `poller`: this poller's URL as the panels see it, for lazy fetches such as `GET /v2/model`.
- Model ids in the top-model lists.

The e-paper panel gets some of the same data as pages: TODAY, LAST 30 DAYS, TOP MODELS (each with a 7/30-day page), BEST TIMES, COMMENTS, ACCOUNT, and a "data stale" status when health says so. E-paper work is on hold, so it has no creator stats.

Color panels are best addressed by their `.local` name (e.g. `color_panel_host: "makermonitor-8e7a8.local"`), which survives DHCP changes.

## Creator stats

With a cookie, the poller also reads MakerWorld's own per-model analytics (the Creator Center data-overview page of each model: daily impressions, views, downloads, prints, likes, collects, boosts, followers gained and points) and each model's comments. This runs in the background at low priority, through the [request limiter](#makerworld-requests), at most once every 10 minutes:

- **Backfill:** one model that has no analytics yet (its last 30 whole days), so a 30-model account fills in over about 5 hours.
- **Refresh:** the 2 models fetched longest ago (when older than an hour).
- **Launch window:** for one older model at a time, its first 7 days after publishing, fetched once and kept.
- **Comment scan:** up to 2 models whose comment count changed since the last scan (all of a model's comments, paged), to count the ones the creator hasn't replied to (`replyByDesignUser`).
- After 3 failures in a row, backfill, launch windows and comment scans pause for an hour. Refreshes stay paced by the limiter.

Everything is cached in `data/creator.json` (gitignored), so restarts don't refetch. Computed per model and for the account:

1. **Conversion:** downloads per view (7 and 30 days), prints per download (lifetime and 30 days).
2. **Engagement:** likes, collects and comments per 100 downloads (lifetime, 7 and 30 days).
3. **Momentum:** yesterday's downloads against the model's average over the 7 days before. Up (≥ 1.25×), down (≤ 0.75×) or flat. **Rising fast:** ≥ 2× and at least 3 downloads. **Stalled:** averaged at least 1 a day, none yesterday and none today so far.
4. **Launch curve:** cumulative downloads on days 1–7 of each launch; the latest launch is compared with the median of earlier launches, with its percentile.
5. **Points:** per model for 7 and 30 days (MakerWorld's attribution), plus the account's points per day (7-day average against the 7 days before: up, down or flat).
6. **Follower sources:** followers gained per model over 7 and 30 days, as MakerWorld attributes them.

Followers are also tracked for the account: the total comes with every poll, today's gain from the daily baseline (`data/daily.json`), and the gains per day for the last 30 days from the same 30-day reach report (no extra request). The color panels and `/api/dashboard` get them as `followers: {total, today, d7, d30, days, series30}`; `series30` is oldest first and ends with today.
7. **Long tail:** today's, 7-day and 30-day downloads from models older than 30 days against newer ones.
8. **Unanswered comments:** the total and the models with the most.

MakerWorld fills a day in only once it is over, so per-model analytics run through yesterday; "today" values come from the poller's own baselines. The state's `creator` block has the top-4 lists; `GET /v2/creator` returns everything (every model's row, account numbers, and `compact` with top-10 lists). `GET /v2/model?id=` adds that model's `stats` (add `&full=1` for its MakerWorld daily rows).

## MakerWorld requests

All MakerWorld traffic from the poller (the regular poll, creator stats, launch windows and comment scans) goes through one limiter in `makermonitor/ratelimit.py`, with the same headers, user agent and cookie as the regular poll:

| | |
|---|---|
| In flight | 1 request at a time |
| Gap | at least 3 s, plus 0–1.5 s of jitter |
| Budget | 120 requests per rolling hour. Background work stops at 80, leaving 40 for the regular poll |
| Background | at most once every 10 minutes, never during a regular poll, up to about 6 requests per run |
| Backoff | per endpoint on 429, 403 or 5xx: 1, 2, 4 … 60 minutes (±20%), or `Retry-After` when longer (up to 6 h); reset by a success |
| Pause | all background work after a 429 anywhere, or while the profile or design list is backing off |

The regular poll keeps its own cadence and only waits for the gap (it skips an endpoint only while that endpoint has a 429 backoff). At the default 10-minute interval a regular poll makes about 5–8 requests (30–50 an hour), and background work up to about 36 an hour. The Next.js build id is cached for 6 hours, which saves a request per poll. Request times and backoffs are saved in `data/ratelimit.json`, so a restart doesn't reset the budget. The log has one line per hour with the count; health shows it live (`health.limiter`). No rate-limit headers (`X-RateLimit-*`, `Retry-After`) have been seen from MakerWorld so far; any that appear are logged and shown in health.

## Dashboard

`http://<poller-host>:8787/` (or `/dashboard`) is a LAN page that works on a phone (two-column tiles and sticky section links there). Open it once as `http://<poller-host>:8787/?token=<api_token>`; the browser then keeps a cookie (180 days). Without the token it answers 401. It is plain HTML/JS with no external assets, follows the system light or dark theme, shows times in 12-hour format, and refreshes every 60 s. It has five views (each has its own link, such as `/#models`):

- **Overview:** today's downloads, prints, boosts, likes and collects (with yesterday's total and an hourly line), top models with a 30-day download sparkline, followers (with a bar per day for 30 days), 7/30-day totals, best times and the creator numbers at a glance.
- **Models:** every model in one sortable table (downloads, views, downloads per view, prints, likes, collects and points for 7 or 30 days, trend, unanswered comments, sparkline), then the creator lists: best converting, most engaging, rising, stalled, launch curve, points and followers by model, and long tail. Model names link to MakerWorld.
- **Activity:** today's events and the latest comments.
- **Health:** each MakerWorld source with its last good fetch, failures and the last error text, the cookie, the request budget and backoffs.
- **Settings:** see below.

Its data comes from `GET /api/dashboard` (same token or cookie), `GET /v2/creator` and `GET /v2/model` (sparklines).

### Settings

The Settings view edits `config.yaml` from the browser: the MakerWorld handle and cookie (status, plus a box to paste one by hand), poll interval and panel page time, color and e-paper panel hosts (with a **Check** button per panel), notification defaults (events, quiet hours, what happens when they end, urgent events, minimum counts) and each notification channel (on/off, events, quiet hours, its own fields, **Send test message**), and the user agent.

- **Save changes** validates everything first (the same checks the poller makes at startup, including each channel's required fields and `${ENV}` references) and shows problems next to the field. Nothing is written until it passes.
- The previous file is kept as `config.yaml.bak-<time>` next to it (the last 10), the new one is written atomically, and comments and quoting in the file are kept.
- Changes apply right away, without a restart. Notification channels keep their quiet-hours queue and alert state across a save.
- Quiet hours are typed as `11:00 PM-7:00 AM` (`11pm-7am` and 24-hour `23:00-07:00` also work) and saved to `config.yaml` as 24-hour.
- Webhook URLs, tokens, keys, ntfy topics and Telegram chat IDs are never sent to the browser. They show as "Saved" and stay as they are unless you type a new value. `${NAME}` references show as written.
- `listen_host`, `listen_port`, `data_dir` and `api_token` stay file-only (a change needs a restart, and a new token would lock the dashboard and extension out).
- If `config.yaml` changed on disk after the page loaded, saving is refused until you reload.
- The settings endpoints (`/api/settings`, `/api/settings/test`, `/api/settings/panel`, `/api/settings/cookie`) take the dashboard token or cookie. Their POSTs also need an `X-MM` header, which the page sends.

## Notifications (optional)

The poller can send activity and health alerts to Discord, Pushbullet, Pushover, ntfy, Telegram, Home Assistant, Apprise (Slack, Matrix, Gotify and 100+ more) or any JSON webhook, with or without a panel. Add channels under `notifications:` in `config.yaml` and check them with:

```bash
.venv/bin/python -m makermonitor --test-notify
```

- Each poll sends at most one message per channel, grouped by event type (downloads, prints, boosts, likes, collects, comments, followers, points), each with its own emoji and color. Health alerts cover an expired cookie and stale data, and their recovery.
- `events` / `exclude` choose what each channel (or all channels) gets. `quiet_hours` mutes a window, `quiet_digest` sends a "while you were away" message when it ends, `min_count` waits for enough activity.
- Secrets can come from the environment as `"${NAME}"`; `config.yaml` is git-ignored either way.

Every option, per-service setup, the webhook format and how to add a new service: [`NOTIFICATIONS.md`](NOTIFICATIONS.md).

## Endpoints (for the extension)

| Path | Method | Auth | Purpose |
|------|--------|------|---------|
| `/health` | GET | no | Liveness |
| `/status` | GET | token | Cookie age, last poll, last error |
| `/auth/cookie` | POST | token | Update MakerWorld cookie jar |
| `/poll/now` | POST | token | Force an immediate poll |
| `/`, `/dashboard` | GET | token once, then cookie | LAN dashboard |
| `/api/dashboard` | GET | token or cookie | Dashboard data (the color state plus health and insights) |
| `/v2/model?id=<design id>[&full=1]` | GET | no (LAN) | One model's daily activity (up to 30 days) and its creator `stats`, for the panels' model detail |
| `/v2/creator` | GET | no (LAN) | Creator stats in full, for the dashboard and panels |

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

1. Profile page `https://makerworld.com/en/@{handle}` (user id) and `GET /api/v1/design-service/published/{uid}/design` for every model's counts (source `models`)
2. Comments for models whose comment count changed (`/api/v1/comment-service/comment`)
3. With a valid cookie: `GET /api/v1/point-service/point-bill/my` for the points balance (source `points`)
4. With a valid cookie: Creator Center `data-overview` (`_next/data/<buildId>/en/my/data-overview/model.json`) for the 30-day reach report (source `reach`); retried once, and the last good report is kept when it fails
5. In the background, with a valid cookie: each model's `data-overview/model/<id>.json` and its comments (see [Creator stats](#creator-stats))
6. Fallbacks: the profile's `__NEXT_DATA__`, then the Creator Center list (up to 10 model details)

If MakerWorld changes HTML/JSON shapes, update `makermonitor/makerworld.py`.

## Docker

The `Dockerfile` and `docker-compose.yml` here run the same poller in a container. `config.yaml` and `data/` live in one mounted folder, `./docker-data`:

```bash
cd poller
mkdir -p docker-data
cp config.example.yaml docker-data/config.yaml   # or your existing config.yaml
cp -a data docker-data/                            # optional: keep cookie, history and creator stats
# edit docker-data/config.yaml
MM_UID=$(id -u) MM_GID=$(id -g) docker compose up -d --build
docker compose logs -f
```

- It uses host networking, so the panels reach it on the host's LAN address and `.local` panel names resolve. Docker Desktop needs host networking turned on (Settings > Resources > Network).
- Set `TZ` (default `America/New_York`) so "today", daily stats and quiet hours match your clock.
- The container runs as your user id, so `docker-data/` stays editable; the dashboard's Settings view saves into `docker-data/config.yaml`.
- Mount the folder, not `config.yaml` alone: saving replaces the file, which a single-file mount can't do.
- Only run one poller per account: stop the systemd service (`systemctl --user disable --now makermonitor-poller`) before starting the container, or they both poll MakerWorld.

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

For a checkout in your home directory, use [`makermonitor-poller.user.service`](makermonitor-poller.user.service) as a user unit, with `loginctl enable-linger` so it keeps running after logout:

```bash
mkdir -p ~/.config/systemd/user
cp makermonitor-poller.user.service ~/.config/systemd/user/makermonitor-poller.service
systemctl --user daemon-reload
systemctl --user enable --now makermonitor-poller.service
loginctl enable-linger "$USER"

systemctl --user restart makermonitor-poller.service   # after config or code changes
systemctl --user status makermonitor-poller.service
journalctl --user -u makermonitor-poller.service -f    # service output; the poll log is data/poller.log
```

Before restarting after code changes, check it imports: `.venv/bin/python -c "import makermonitor.server"`.

Point the Chrome extension at `http://<linux-host-lan-ip>:8787`.
