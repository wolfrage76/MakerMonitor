# Architecture

```
┌─────────────────────┐     cookie POST      ┌──────────────────────┐
│ Chrome extension    │ ──────────────────► │ Python poller        │
│ (cookie courier)    │                      │  /auth/cookie        │
│ triggers on MW tabs │                      │  poll loop           │
└─────────────────────┘                      │  MakerWorld scrape   │
                                             └──────────┬───────────┘
                                                        │ optional notifications (HTTPS)
                                                        ├──► Discord, Pushbullet, Pushover, ntfy, Telegram,
                                                        │    Home Assistant, Apprise, generic webhook
                        ┌───────────────────────────────┴──────────────────────────┐
                        │ /sync /notify /status                                    │ /v2/state /v2/event
                        ▼                                                          ▼
             ┌──────────────────────┐                               ┌──────────────────────────┐
             │ CrowPanel e-paper    │                               │ Color panel              │
             │ WiFi + HTTP + EPD    │                               │ Today, feed, web console │
             └──────────────────────┘                               └────────────┬─────────────┘
                                                                                 │ GET /v2/model, /v2/settings
                                                                                 ▼ (poller, lazy / every 15 s)
```

The poller also serves the LAN dashboard (`/`, `/api/dashboard`, token then cookie), its Settings endpoints (`/api/settings*`, which edit `config.yaml` and apply it live through `AppContext.apply_settings`) and `/v2/creator` (creator stats).

## MakerWorld requests

Everything the poller sends to MakerWorld goes through one limiter (`poller/makermonitor/ratelimit.py`):

- One request in flight, at least 3 s apart plus up to 1.5 s of jitter, and a rolling budget of 120 requests an hour.
- The regular poll ("core") only waits for the gap. Background work (per-model analytics, launch windows, comment scans) runs at most once every 10 minutes, waits while a core poll is running, and only uses what is left of the budget after a 40-request reserve for core polls.
- Each endpoint backs off on 429, 403 or 5xx: 1, 2, 4 ... up to 60 minutes with jitter, or `Retry-After` when longer. After a 429 anywhere, or while the profile or design list is backing off, all background work pauses. The 30-day account report is refused now and then on its own, so it only backs off itself.
- Request times and backoffs are saved in `data/ratelimit.json`, and the creator stats cache in `data/creator.json`, so a restart doesn't refetch or reset the budget.
- `health.limiter` in the panel state (and on the dashboard) shows `requests_last_hour`, `budget`, `backoff_until` per endpoint and any rate-limit headers MakerWorld sent.

## Firmware updates (color panels)

An update (ArduinoOTA, web console upload or GitHub release) first draws an "Updating firmware... Do not unplug" screen, stops the speaker, then switches the RGB panel off: octal PSRAM and flash share the cache, and the panel DMA would fault during flash writes. The screen stays dark until the panel restarts. If the display is off with no update running for 20 s (an upload that stalled), the panel restarts itself; the reason is logged at the next boot. A 30 s loop watchdog reboots a hung panel.

## Trust boundaries

- Session cookies never leave the LAN (extension → poller).
- Poller stores `data/cookie.txt` mode `0600`.
- Panels only receive aggregate stats / event JSON — no cookies.
- `poller/config.yaml`, `poller/data/`, and private OTA overrides stay off GitHub.
- Notifications send only stats, model names and comment text to the services you configure, never cookies. Their tokens and webhook URLs live in `config.yaml` or environment variables (`"${NAME}"`) and are not written to the log.

## E-paper failure UX

| Condition | Panel status line |
|-----------|-------------------|
| No first sync yet | `WAITING SYNC` |
| Public scrape OK, no cookie | `AUTH public only · open MakerWorld` |
| Cookie rejected / CF | `AUTH open MakerWorld to refresh cookie` |
| MakerWorld data older than 3 poll intervals | `DATA STALE` chip, `data stale since HH:MM` |
| Scrape/transport error | `ERR …` |
| Healthy | `OK synced · cookie Xm` |
