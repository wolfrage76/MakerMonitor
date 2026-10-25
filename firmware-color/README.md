# MakerMonitor Color

Color stats panel firmware. It shows today's Boosts, Likes, Collects, Downloads and Prints with 7- and 30-day context, pops a banner as each event (or a new comment) arrives, and has pages for the feed, top models, trends, points, reach, comments, best times, creator stats and status.

| Board | Screen | Controls | Env |
|---|---|---|---|
| Waveshare ESP32-S3-LCD-3.16 | 820×320 | KY-040 knob | `ws316` |
| Elecrow CrowPanel ESP32-S3 HMI 4.3" | 480×272 | resistive touch | `crowpanel_43` |
| Elecrow CrowPanel ESP32-S3 HMI 5.0" | 800×480 | capacitive touch | `crowpanel_50` |
| Elecrow CrowPanel ESP32-S3 HMI 7.0" | 800×480 | capacitive touch | `crowpanel_70` |

**Every board has the same features.** Only the layout and the controls differ: 800×480 gets taller cards (Today adds 7D/30D rows, an hourly chart and a Recent activity card), 480×272 smaller type and fewer columns, and the 820×320 Waveshare a wide layout driven by the knob. Each board's pins, timings and controls are in `include/boards/`. The 7" CrowPanel is the panel this firmware is tested on; the other builds use the same code with their own board headers.

It is a thin client: the Python poller does all MakerWorld work and pushes stats to it. The e-ink panel keeps working alongside (its firmware is on hold; it doesn't get creator stats).

![Today](../docs/screens/today.png)

Current captures of every page are in [`../docs/screens/`](../docs/screens/) (see [Screenshots for testing](#screenshots-for-testing)); the older images in `firmware-color/docs/` predate the current UI.

## Flash

1. Install PlatformIO (VS Code extension or `pip install platformio`).
2. `cd firmware-color && rm -f sdkconfig.defaults && pio run -e <env> -t upload` over USB-C (env from the table above).
3. On first boot (no saved network) the **Wi-Fi page** opens: pick your network and enter its password (see [Wi-Fi setup](#wi-fi-setup)).
4. The Today header shows the panel's address until the poller sends data.

### Build notes (read before building)

- **Delete `sdkconfig.defaults` whenever you switch envs.** Every env uses pioarduino `custom_sdkconfig` (display interrupt code in IRAM, flash size, octal PSRAM, I2S). pioarduino keeps one rebuilt Arduino core for all envs and decides whether to rebuild it from a hash in that file; a stale one links the wrong core. After changing a `custom_sdkconfig` line, also delete `sdkconfig.<env>` (it overrides the new defaults).
- **Check the image before uploading.** A build linked against the stock core crashes on the first flash write (saving state, NVS, OTA) and shows "waiting for poller" in a reset loop:

  ```bash
  ~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm .pio/build/<env>/firmware.elf \
    | grep -E ' [tT] (gdma_start|gdma_reset|gdma_append|gdma_hal_append|rgb_lcd_default_isr_handler|gdma_default_tx_isr|i2s_dma_tx_callback)$'
  ```

  On the 7" also check `i2s_dma_tx_callback` (the speaker). Every line must be present with an address starting `0x403` or `0x4037`. `0x42…` (flash) or a missing symbol means the fix is not in that image.
- **The 7" runs code from PSRAM (XIP).** `crowpanel_70` sets `SPIRAM_XIP_FROM_PSRAM` (plus `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA`) and `LCD_RGB_RESTART_IN_VSYNC`; `/diagnostics` reports `"xip": true` when it is active. It costs about 1.7 MB of PSRAM and nothing measurable in page slides (23 fps either way).
- **Bump the version before every flash.** Raise the patch number of `FW_VERSION` in `include/fw_version.h` (0.4.17 → 0.4.18) before each USB or OTA flash, so the version at the bottom right of the screen, the web console and `/v2/settings` always tell builds apart.
- Don't add build flags through `PLATFORMIO_BUILD_FLAGS`; that also links the stock core. Put temporary defines in the source.

### USB, OTA and recovery

- **USB:** `pio run -e <env> -t upload --upload-port /dev/ttyUSB0`. If the board doesn't show up (no CH340 in `lsusb`), use a data cable and a rear port, or hold **BOOT**, tap **RESET**, release BOOT for download mode. That also recovers a panel stuck in a crash loop.
- **OTA:** the `<env>_ota` environments, or espota directly:

  ```bash
  ~/.platformio/penv/bin/python ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py \
    -i <panel-ip-or-name> -p 3232 -f .pio/build/<env>/firmware.bin
  ```

  OTA is sometimes refused while the panel is busy (Wi-Fi scan, a redraw, the GitHub check about 2 minutes after boot); retry once it settles. Wi-Fi power saving is off from 0.4.3: with it on, LAN round trips ran 60–800 ms and uploads (one acknowledgement per 1.4 KB) crawled and failed part way. On the 7" (built with `SPIRAM_XIP_FROM_PSRAM`, so flash writes leave the cache on) an update shows a full-screen **Updating firmware** page with a percentage and progress bar while the display keeps running; the pixel clock is halved meanwhile so flash writes don't starve the PSRAM scan-out, and the screen only goes dark for the final boot-partition switch. Other panels stop the speaker and switch the display off for the whole update (their scan-out would fault during flash writes) and stay dark until they restart. If an update stalls, or the screen is left off or on the update page with no update running for 20 s, the panel restarts itself on the old firmware; the next boot logs the reset reason and why the update ended, and shows a toast. Every path (ArduinoOTA, web upload, GitHub) ends in one wrapped `esp_ota_set_boot_partition`, which logs the switch and restarts on the old firmware if it fails. A boot splash ("MakerMonitor / starting") shows about 0.2 s after power-on, before the pages are built. Set `upload_port` for the `_ota` envs in gitignored `platformio_override.ini`. There is no firmware password in the repo; the web console can set one (leave it blank for LAN uploads).
- **HTTP:** `POST /update` with a `.bin` (also from the web console's Firmware tab).
- **GitHub releases:** the console and the on-screen Update page check `wolfrage76/MakerMonitor` for release assets named in each board header (`firmware-crowpanel-70.bin`, …). While the repository is private this check gets HTTP 404; automatic checks then pause (remembered across reboots) until a manual *Check now* succeeds, so the failing TLS request doesn't run every hour. When a newer release is known, the footer version turns into an accent "v0.4.x → v0.4.y" chip that opens the update page.

## Wi-Fi setup

All boards set up Wi-Fi on the panel itself, in station mode. There is no automatic setup hotspot (the old WiFiManager portal is gone).

- **Wi-Fi page** (Settings → *Wi-Fi*, the "Wi-Fi not connected" banner, or first boot): shows the current network, scans and lists networks by signal (signal bars, *connected* / *saved* / *secured* / *open*), plus *Other network…* for a hidden SSID and *Rescan*.
- **Password entry:** touch boards get an on-screen keyboard with Show/Hide. Knob boards get a **character wheel**: turn to scroll the characters, press to add one; the first slots are ⌫ (delete), the next character set (a–z, A–Z, 0–9, symbols), Show/Hide and ✓ (connect, or move from the name to the password). Hold to cancel. A saved network can reconnect with the password left empty.
- **Connecting** times out after 15 s and reports *Wrong password*, *Network not found* or *Timed out*; on failure the previous network is restored. Credentials are saved by the Wi-Fi driver, so the next boot reconnects.
- **If the saved network is lost** (at boot or later), the panel keeps running and retries in the background after 10 s, 30 s, then every 60 s. After 20 s offline a **"Wi-Fi not connected - tap to choose network"** banner appears; it opens the Wi-Fi page.
- **Set up from phone** (a row on the Wi-Fi page; started by you only): the panel starts a WPA2 hotspot `MakerMonitor-XXXX` (last 4 hex digits of the MAC) with a fresh random 8-character password and shows a **Wi-Fi join QR** with the SSID and password beside it. Once a phone joins, the QR changes to **`http://192.168.4.1`** in case the captive-portal page doesn't pop up. The page lists the scanned networks (or type an SSID), takes the password and connects; the result shows on the phone and on the panel. The hotspot stops after a successful join, after 10 minutes, or on Cancel (hold on knob boards).

## Hostname and mDNS

The panel is `<name>.local` on the LAN. The name is set in the web console (Network): blank means `makermonitor-` plus the last 5 hex digits of the MAC (e.g. `makermonitor-8e7a8`); the 7" here is set to `makermonitor`, so it is `makermonitor.local`. Names are cleaned to lowercase `a–z`, `0–9` and `-`, at most 32 characters, no leading or trailing hyphen. **Announce on the network (mDNS)** can be switched off. Changes apply immediately (no reboot) and the name is announced again after every Wi-Fi reconnect, advertising `_http._tcp` and `_arduino._tcp` (OTA). The Status page shows the effective `<name>.local`, or "(mDNS off)". The same name is the DHCP hostname from the next lease.

## Point the poller at it

In `poller/config.yaml`:

```yaml
color_panel_host: "makermonitor.local"   # the panel's .local name (or its IP)
color_panel_port: 80
```

Using the `.local` name keeps working when DHCP hands out a new address. Several panels: separate them with commas. Every panel gets the same stats; the poll interval follows whichever panel's setting changed last. Restart the poller (`systemctl --user restart makermonitor-poller.service`, see [`../poller/README.md`](../poller/README.md)).

## Controls

| | Touch | Knob |
|---|---|---|
| Change page | Swipe left/right; the page follows your finger (see below) | Turn |
| More on the page | Swipe up/down (Feed filter, Top mode, Trend stat, Best-times metric, Creator view, Comments page) | Press (same actions) |
| Tap targets | Chips, page segments (jump to a page), Top rows and Creator list rows (model detail), Creator tiles (what the number means), the footer dot (Status), the points chip (Points page), an update chip (Firmware update), Status buttons | — |
| Top models | Tap a model for its detail | Press to select, turn through the Today/7 days/Lifetime chips and the models, press to open, hold to leave |
| Status | Tap the footer dot (bottom left, any page) | Press on Today |
| Menu | Hamburger (top right) | Hold, then turn and press |
| Back from Settings/Status/detail pages | Back button or long press | Hold |
| Banner | Tap or swipe up to dismiss | Press to clear, turn for the next |

Empty background does nothing when tapped. The first touch or knob input on a dimmed or dark screen only wakes it.

**Swipes:** a horizontal drag moves the page with your finger and shows the neighbour beside it. It changes page past 80 px (10% of the width on narrower panels), on a quick swipe (30 px in under 350 ms) or when fast (≥ 0.3 px/ms and at least 25 px, taking the larger of the average speed over the gesture and the peak over its last ~50 ms); otherwise it snaps back. A move counts as horizontal unless |dx| is under 0.8 × |dy|. Slides are drawn by a compositor from pre-rendered neighbour pages in PSRAM, so they stay smooth (rotated panels such as the Waveshare use a short LVGL slide). Build with `-DDEBUG_SWIPE=1` (in the source, see Build notes) to print each swipe to Serial (`swipe dx -96 180ms avg -0.53 peak -0.71: commit (quick)`) for tuning; the device log no longer records swipes. Page slides log their frame rate (`slide 7 frames in 308 ms (23 fps)`).

## Screens

Today, Live feed, Top models, 7-day trend, Points, Reach (30 days), Comments, **Best times**, **Creator**, and (from the menu) Status, Settings, About and Update. Page segments at the bottom show where you are.

- **Today header:** the title and date on the left, the 12-hour clock centred and the hamburger on the right. The points chip (tap: Points page) sits centred between the clock and the hamburger, placed from its real width, on the title's centre line; where that gap is too narrow it hides. Unanswered comments are on the Creator page's Reply tab.
- **Footer** (every page, on the line of the page segments): a **status dot** at the bottom left (green = synced, amber = cookie needed or MakerWorld data stale, red = cookie expired or an error; tap for Status) and the firmware version at the bottom right, which becomes an update chip when a newer release is known.
- **Followers:** on 800×480 the Today page shows the follower count and today's gain at the top right of Recent activity; every panel has the Creator page's Followers tab.
- **Today (800×480):** each tile has today's number, yesterday, **7D** (white) and **30D** (cyan) totals, an hourly chart in 2-hour bars with white 7-day and cyan 30-day average lines, and the lifetime total; a **Recent activity** card below lists the newest events.
- **Best times:** activity by hour of day (24 bars) and by weekday over the last 30 days, for downloads, prints or boosts, with the best hour and day highlighted. Shows "Collecting data (N of 7 days)" until a week of history exists.
- **Model detail:** from Top models. 7- and 30-day totals per stat and daily download bars, fetched from the poller when opened. With creator stats, a second line adds downloads per view, prints per download, engagement per 100 downloads, yesterday against the 7-day average (rising / stalled), points and followers for 7 and 30 days, and unanswered comments.
- **Creator:** the poller's creator stats in six views. A row of summary tiles sits under the header and the views are a segmented control (its own row on 480×272). Tap a segment, swipe up/down, or press the knob: **Convert** (downloads per view and engagement per 100 downloads, top 4 each, plus account numbers), **Trend** (rising and stalled models: yesterday against each model's own 7-day average), **Launch** (the latest launch's cumulative downloads on days 1–7 against the median launch, with its percentile, plus the long-tail share: downloads from models over 30 days old today, 7 and 30 days), **Earn** (points over the same periods: Account 7d (with its trend) and 30d, which is the whole balance change including profile points and spending, and Models 7d and 30d, what MakerWorld credits to models with analytics so far; points and followers per model), **Followers** (total, today, 7 and 30 days, a bar per day for 30 days with losses in red, and the models followers came from), **Reply** (the models with the most unanswered comments and the total). A caption at the bottom says "Analytics for N of M models" while the poller backfills MakerWorld analytics (one model about every 10 minutes); an empty list says so in its card. Tap a list row for that model's detail page, or a tile for a one-line definition. Model names arrive with up to 60 characters and are cut with "..." only where they don't fit.
- **Status:** poller sync with the age of each MakerWorld source (models, reach, points), cookie, next poll, day baseline, network (`<name>.local`, SSID, signal) and device/firmware.
- **Health banner:** "Cookie expired - refresh it in the extension" (red) or "MakerWorld data stale since 9:05 PM" (amber), from the poller's health block; tap it for Status. "Wi-Fi not connected" takes priority and opens the Wi-Fi page.

### Screenshots for testing

`tools/capture_screens.sh [host] [out-dir]` (repo root) walks every page, Creator tab and Settings picker through `/v2/ui/show`, waits 1.5 s each and saves `/screenshot.png` to `docs/screens/<name>.png`. Pages: `today`, `feed`, `models`, `trend`, `points`, `reach`, `comments`, `best`, `creator`, `status`, `settings`, `about`, `update`, `wifi`, `guide`. Tabs (`creator`): `convert`, `trend`, `launch`, `earn`, `followers`, `reply`. `open`: on `settings`, a section (`display`, `quiet`, `alerts`, `network`, `device`) to scroll to, `reset`, `quiet_from`, `quiet_until` or a setting key (`bright`, `notify`, `poll`, ...); on `models`, a design id for its detail page. Both endpoints use the web console password when one is set (`MM_AUTH=admin:<password>` for the script).

## First run

Once a new panel is on Wi-Fi but has had no data for 15 s, it shows **Connect the poller**: its `<name>.local` and IP, the `color_panel_host` line to put in `poller/config.yaml`, and a QR code for the web console. *Skip* hides it until the next boot, *Don't show again* for good; the first state push ends it for good.

## Settings

Saved on the panel: brightness, auto-cycle, which events notify, quiet hours, **night mode**, poll interval (the poller reads it within 15 s), return-to-Today delay, banner time, dim after, and on the 7" sound and volume. The page is one scrolling list in sections (Display, Quiet hours, Alerts, Network, Device), two settings per line on 800 px panels. Option settings open a short list to pick from, on/off settings are switches, and *Wi-Fi* shows the current network and opens the Wi-Fi page. Device links to Firmware update, Status and About.

- **Brightness** opens a slider that previews on the panel as you move it (Done saves; tapping outside puts it back). *Notify on* and *During quiet hours* carry a one-line hint. **Network** shows the address and hostname. **Device → Reset settings** (with a confirm) puts display, quiet-hours, alert and polling settings back to their defaults and keeps Wi-Fi, hostname, password and update preferences. The page keeps its scroll position when you come back to it.
- **Quiet hours:** an on/off switch plus *Quiet from* and *Quiet until*, each set with hour, minute and AM/PM rollers (15-minute steps; stored as minutes since midnight); the knob turns the highlighted field and a press moves on, then saves. The window may cross midnight; equal times mean no quiet time. The web console has the same switch and two 12-hour time lists. Panels upgraded from 0.4.3 keep their preset (for example 10:00 PM–7:00 AM).

- **Night mode** (Screen off / Dim, default Screen off): during Quiet hours, once the panel has been idle for the "Dim after" time (or 30 s), the backlight turns off (or very low). Any touch or knob input wakes it for the normal dim timeout; that first input does nothing else. Banners stay silent during quiet hours.
- **Time:** every time on the panel and in the web console uses the 12-hour clock ("9:05 PM"; hour-only labels "9 PM", and "12a 6a 12p 6p" on the small Today charts). There is no time zone or 12/24-hour setting: clocks and day boundaries use the UTC offset the poller sends with every update (UTC until the first one).
- **Stored settings** live in one struct in `src/settings.cpp` with their defaults; the NVS keys are the ones earlier versions used, so an upgrade keeps every value.

## How "today" works

MakerWorld only gives lifetime totals. At the first poll of each day the poller keeps the last pre-midnight snapshot as the baseline; today = now − baseline. If the poller wasn't running at midnight, the day starts when it did; the Status page's Day base row shows which ("since midnight" or the start time). The poller stores this in `poller/data/daily.json`, with daily totals, per-day hourly buckets (for best times) and per-model daily baselines (for model detail), so reboots of either side lose nothing.

## Memory and diagnostics

- LVGL widgets live in **PSRAM** (`include/lv_conf.h`); only the two draw buffers stay in internal RAM. On the 7" about 154 KB of internal RAM stays free (about 150 KB at the lowest point); the app image copied into PSRAM for XIP leaves about 3.3 MB of PSRAM free. Pickers and sheets are built when opened and deleted when closed.
- A 10-line bounce buffer was tried on the 7" and left out: without `LCD_RGB_ISR_IRAM_SAFE` the panel object sits in PSRAM, GDMA refuses the refill callback ("user context not in internal RAM") and the screen stays black, and it would cost 32 KB of internal RAM. Halving the pixel clock during updates is what keeps the progress screen from shifting.
- The serial log prints `heap @boot/display/ui/sound/wifi/services/idle` lines at start-up, and `ALLOC FAILED: <bytes> caps … in …` if an allocation ever fails.
- `GET /diagnostics/logs` (web console) shows the device log: the reset reason at boot (and why the last update ended, when it didn't finish), Wi-Fi scans and connects, mDNS, hotspot start/stop with heap, page-slide frame rates, pixel clock changes for updates, the boot-partition switch, screenshots, GitHub checks with heap. Swipes are not logged (Serial only, with `-DDEBUG_SWIPE=1`).
- `GET /diagnostics` returns JSON: firmware, uptime, last reset reason, when this version was installed ("installed 3 h ago", stamped once per version when the clock is first set; also in the web console), internal RAM now / lowest / largest block, PSRAM free, RSSI, host, whether the display is on and whether XIP is active.
- `GET /v2/settings` also reports `heap` (internal RAM free now), `heapMin` (lowest since boot) and `rssi`. The state push is parsed in PSRAM, so a push no longer dips internal RAM (about 159 KB free idle and during pushes).
- A 30 s loop watchdog reboots the panel if the main loop hangs (updates feed it while they run).
- "Poll now" (Status) only bumps a counter in RAM that the poller reads from `/v2/settings` within 15 s; it doesn't write to flash. After a reboot the counter starts over, which the poller may take as one extra poll.

## If something looks wrong

All of these are one-line changes in the board's header in `include/boards/` (`ws316.h` for the Waveshare):

- **Upside down:** `LCD_ROTATION` → `LV_DISP_ROT_270`.
- **Blank or garbled screen:** the ST7701 init table in `src/display.cpp` came from a working ESPHome config for this board. Compare it with Waveshare's `07_LVGL` demo and swap it in if they differ. Also try `LCD_PCLK_NEG 1`.
- **One click moves two screens:** `KNOB_STEPS_PER_DETENT 2`. **Backwards:** `KNOB_REVERSE 1`.
- **Backlight inverted:** `LCD_BL_ACTIVE_HIGH 0`.
- **CrowPanel picture drifts or flickers:** lower `LCD_PCLK_HZ` (e.g. 12 or 8 MHz).
- **Touch lands in the wrong place:** set `TOUCH_FLIP_X`, `TOUCH_FLIP_Y` or `TOUCH_SWAP_XY` to 1. On the 4.3", adjust `TOUCH_RAW_X0/X1/Y0/Y1`.
- **CrowPanel touch dead:** the serial log prints `GT911 found/not found`. Board v3 needs the PCA9557 release in `src/touch.cpp`, which runs automatically when the chip isn't found.
- **Resets right after a poller push, or "waiting for poller" in a loop:** the image is missing the IRAM fix; see [Build notes](#build-notes-read-before-building).

## Wiring the knob (Waveshare 3.16")

| KY-040 | Board |
|---|---|
| CLK | GPIO 43 (UART header TX) |
| DT | GPIO 44 (UART header RX) |
| SW | GPIO 0 (BOOT button pad) |
| + | 3.3 V |
| GND | GND |

Serial logs go over USB, so the UART header pins are free. Holding the knob while plugging in puts the board in flash mode (same as BOOT). Optional: a 1 kΩ resistor in series on CLK protects GPIO 43, which the boot ROM briefly drives.

## Speaker

The 7" CrowPanel has an NS4168 I2S amplifier on its SPK connector (passive 8 Ω speaker) for event chimes; the boot log prints `I2S OK`. The other boards in this tree have no speaker.

## Screenshots

`tools/capture_screens.sh` (see [Screenshots for testing](#screenshots-for-testing)) captures every page, Creator tab and Settings picker from a running panel into `docs/screens/`; run it after a flash and look the images over. The web console's Status tab also has a Screenshot button.

## Files

| File | What |
|---|---|
| `include/boards/*.h` | Per-board pins, timings and controls |
| `include/lv_conf.h` | LVGL options (PSRAM heap, enabled widgets and fonts, snapshot) |
| `include/fw_version.h` | Firmware version |
| `src/display.cpp` | RGB panel + LVGL driver, page-slide compositor, framebuffer for screenshots, half pixel clock during updates (ST7701 init and rotation for the Waveshare) |
| `src/touch.cpp` | GT911 / XPT2046 touch as an LVGL pointer (a waking touch is swallowed) |
| `src/knob.cpp` | Encoder + button (short / long press) |
| `src/model.cpp` | JSON → `State` / `Event` (incl. health, insights, creator stats, model ids), parsed in PSRAM |
| `src/ui.cpp` | All screens (incl. Creator), banners, Wi-Fi page, phone setup, Settings page and its sheets, first-run guide, boot splash, update progress screen, `/v2/ui/show` page switching; layout adapts to the screen size |
| `src/settings.cpp` | Saved settings in one typed struct (menu options, quiet window, hostname, mDNS, update preferences, install stamp), NVS |
| `src/main.cpp` | Wi-Fi (station, retries, setup hotspot, no power save), HTTP API and web console, mDNS, OTA (progress screen on XIP builds, display-off guard on the others, the boot-partition wrap), diagnostics, screenshots, watchdog, state cache |
| `../tools/capture_screens.sh` | Screenshots of every page through `/v2/ui/show` and `/screenshot.png` |
| `src/sound.cpp` | Event chimes (I2S boards) |
| `src/icons.c` | Stat icons (made by `tools/gen_icons.py`) |

HTTP API: `GET /` (web console: Status with live device numbers, Firmware with upload progress and a reconnect spinner, Settings grouped like the panel with 12-hour quiet times, Network with a password Show/Hide and a warning while no password is set; on the setup hotspot, the phone setup page), `POST /v2/state`, `POST /v2/event`, `GET /v2/settings` (poll interval, poll-now counter, firmware, board, heap, RSSI, quiet hours on/start/end in minutes), `GET /ping`, `POST /update` (firmware upload), `GET /diagnostics/logs` (`DELETE` clears it), `GET /diagnostics` (JSON: firmware, uptime, last reset reason, when this version was installed, internal RAM and PSRAM, RSSI), `GET /screenshot.png` (what the panel shows, in its native orientation), `POST /v2/ui/show?page=<name>[&tab=<name>][&open=<name>]` (switch the screen for tests and screenshots; returns `{"ok":true,"shown":"creator/launch"}`; `&slide=1` slides there like a swipe and logs the frame rate), `GET /v2/ui/pages` (the valid page, tab and `open` names), `POST /wifi-setup` (opens the Wi-Fi page on the panel), `POST /reset-wifi`, `POST /poll-now`, `POST /save` (settings, incl. `quieton` and `qstart`/`qend` in minutes), `POST /save-host` (hostname, mDNS), `POST /save-ota` (update preferences), `POST /save-password` (console password), `POST /github-update?action=check|install`, `GET /update` (upload form), plus `/ap-connect` and `/ap-status` for the phone setup page. Everything except `/ping`, `/v2/state`, `/v2/event`, `/v2/settings`, `/v2/ui/pages` and the phone-setup endpoints uses the console password when one is set (username `admin`).
