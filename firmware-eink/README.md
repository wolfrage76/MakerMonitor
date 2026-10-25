# CrowPanel e-ink firmware

> **On hold.** Work is focused on the color panels for now. The e-ink firmware is unchanged apart from what is described here (the BEST TIMES and per-model 7/30-day pages, the DATA STALE chip and night mode). The poller's newer features, such as creator stats, are not shown on e-paper.

Targets the **Elecrow CrowPanel ESP32 2.13″ e-paper** (JD79661 controller, 250×122 landscape).

## Board settings (Arduino IDE equivalent)

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM**
- USB CDC On Boot: **Enabled**
- Flash size: **8MB**

## Flash with PlatformIO

```bash
cd firmware-eink
pio run -t upload          # USB, /dev/ttyUSB0
pio device monitor
```

Later upgrades can go over WiFi. The flash layout already has two app slots, so this does not erase WiFi settings.

```bash
pio run -e crowpanel_213_ota -t upload
```

The hostname is `makermonitor-` plus the last five hex digits of the board MAC (`makermonitor-e3b7d.local`). The panel reboots into the new firmware when the upload finishes. A refresh takes about 1.4 s and can delay the start of an upload; if it fails, run the command again.

Set a different address in gitignored `platformio_override.ini`. A password belongs in gitignored `include/ota_secret_local.h`, with the same value as `--auth` in that override. Public builds leave the password blank.

The same image can be posted directly:

```bash
curl -F "firmware=@.pio/build/crowpanel_213/firmware.bin" \
  "http://makermonitor-e3b7d.local/update?token=<password, omit if blank>"
```

## First boot

1. Connect to WiFi AP `MakerMonitor-Setup` (open captive portal).
2. Join your LAN WiFi.
3. Display shows IP address and `WAITING SYNC`.

## HTTP API (compatible with original monitor)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/ping` | GET | Health check |
| `/stats` | GET | Current totals JSON |
| `/sync` | POST | Set baseline totals |
| `/notify` | POST | Event + optional totals |
| `/status` | POST | Auth/sync status line |
| `/test` | GET | Fake download notification |
| `/reset` | GET | Clear saved stats |
| `/reset-wifi` | GET | Clear WiFi credentials |
| `/resync` | GET | Mark uninitialized |
| `/pages` | POST | Section/page layouts from the poller (saved to flash) |
| `/nav?key=up\|down\|ok\|home\|hold\|exit` | GET | Same as pressing a button (`hold` = hold HOME); returns mode/section/page |
| `/v2/settings` | GET | `pollSec`, `pollNow` sequence, firmware, board (polled by the poller) |
| `/sound?on=1\|0&test=like` | GET | Enable buzzer / play a test chirp |
| `/update?token=` | POST | Flash a firmware `.bin` over HTTP (see above) |

## Sections and buttons

The poller pushes sections: TODAY (since midnight, with yesterday underneath),
LAST 30 DAYS, TOP MODELS (one model per page, each followed by a page with its
last 7 and 30 days), BEST TIMES (downloads and prints by hour of day and by
weekday over the last 30 days, best hour and day marked; "Collecting data"
until 7 days are stored), COMMENTS (one per page) and ACCOUNT. The firmware inserts its own ALL TIME section (totals, points,
account, sync age) right after TODAY. The header band shows the section index
("2/6") and one square per page. Pages auto-cycle within the current section.
Text you need to read is 16 px or larger; there is no bold below 24 px because
the double-strike fills in M and W.

**TODAY page 1 is home.** The panel boots there, and returns there on HOME,
when an alert ends, when you leave the menu, and after 2 minutes idle anywhere
else.

| Button | GPIO | Pages | Menu / Settings / Status |
|--------|------|-------|--------------------------|
| Wheel up | 6 | Previous section | Move up |
| Wheel down | 4 | Next section | Move down |
| Wheel press | 5 | Next page | Open / change value / poll now |
| HOME (short) | 2 | TODAY page 1 | TODAY page 1 |
| HOME (hold 0.7 s) | 2 | Open MENU | Open MENU |
| EXIT | 1 | Pause/resume page cycling | Back |

Any button dismisses an active alert first.

### Menu

Hold HOME for **Settings** and **Status**.

**Settings** (saved in NVS):

| Setting | Options | Default |
|---------|---------|---------|
| Page cycle | Auto (poller's cycle) / Off / 15 / 30 / 60 / 120 s | Auto |
| Poll every | 5 / 10 / 15 / 30 / 60 min (the poller follows it) | 10 min |
| Alerts | On / Off | On |
| Alert time | 10 / 20 / 40 s | 20 s |
| Sound | On / Off | Off |
| Quiet hours | Off / 22-07 / 23-07 / 00-07: night mode. The panel does not refresh at all (no auto-cycle, poll updates, alert screens or chirps); a key press still redraws it. Totals keep updating in memory | Off |
| Deep refresh | every 4 / 8 / 16 refreshes: an extra white pass that clears ghosting | 8 |

The JD79661 controller has no partial refresh, so every frame (menus
included) is a full ~1.4 s refresh.

**Status** shows Wi-Fi (SSID + RSSI), IP, last sync, cookie state, poll
interval and firmware version. PRESS asks the poller to poll now.

## Buzzer (optional)

Passive piezo buzzer between **IO40** and **GND** on the GPIO_D header (add a
~100 Ω series resistor). Off by default; turn it on in Settings (or with
`/sound?on=1`). Each event
type (like, boost, comment, download, print, points) has its own chirp.

### `/sync` body

```json
{
  "accountName": "you",
  "totalDownloads": 1234,
  "totalPrints": 567,
  "totalBoosts": 89,
  "totalPoints": 2468.5
}
```

### `/notify` body

Same shape as [makerworld-live-monitor](https://github.com/aquascape123/makerworld-live-monitor) (`type`, `modelName`, `value`, … plus optional `currentTotal*`).

### `/status` body

```json
{ "auth": "ok", "message": "synced 2m ago", "lastPollAgeSec": 120 }
```

`auth`: `ok` | `needed` | `stale` | `error`. `stale` (the poller's MakerWorld data is older than 3 poll intervals) shows a **DATA STALE** chip in the header, like `AUTH NEEDED` and `ERROR`.

## Pins

| Signal | GPIO |
|--------|------|
| EPD power enable | 7 (HIGH) |
| SCK | 12 |
| MOSI | 11 |
| RES | 10 |
| DC | 13 |
| CS | 14 |
| BUSY | 9 |

## Libraries

Elecrow's v1.2 JD79661 `EPD` driver in `lib/CrowPanelEPD` (bit-banged SPI; every frame is a full refresh). ArduinoJson and WiFiManager via PlatformIO.
