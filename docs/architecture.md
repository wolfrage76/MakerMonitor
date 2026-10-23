# Architecture

```
┌─────────────────────┐     cookie POST      ┌──────────────────────┐
│ Chrome extension    │ ──────────────────► │ Python poller        │
│ (cookie courier)    │                      │  /auth/cookie        │
│ triggers on MW tabs │                      │  poll loop           │
└─────────────────────┘                      │  MakerWorld scrape   │
                                             └──────────┬───────────┘
                        ┌───────────────────────────────┴──────────────────────────┐
                        │ /sync /notify /status                                    │ /v2/state /v2/event
                        ▼                                                          ▼
             ┌──────────────────────┐                               ┌──────────────────────────┐
             │ CrowPanel e-paper    │                               │ Color panel              │
             │ WiFi + HTTP + EPD    │                               │ Today, feed, web console │
             └──────────────────────┘                               └──────────────────────────┘
```

## Trust boundaries

- Session cookies never leave the LAN (extension → poller).
- Poller stores `data/cookie.txt` mode `0600`.
- Panels only receive aggregate stats / event JSON — no cookies.
- `poller/config.yaml`, `poller/data/`, and private OTA overrides stay off GitHub.

## E-paper failure UX

| Condition | Panel status line |
|-----------|-------------------|
| No first sync yet | `WAITING SYNC` |
| Public scrape OK, no cookie | `AUTH public only · open MakerWorld` |
| Cookie rejected / CF | `AUTH open MakerWorld to refresh cookie` |
| Scrape/transport error | `ERR …` |
| Healthy | `OK synced · cookie Xm` |
