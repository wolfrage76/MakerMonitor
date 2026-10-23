# MakerMonitor Cookie Courier

Chrome MV3 extension that watches MakerWorld cookies and POSTs them to your LAN poller when they change.

It does **not** scrape stats or talk to the CrowPanel. That is the poller’s job.

## Install (Chrome / Edge / Brave)

1. Open `chrome://extensions`
2. Enable **Developer mode**
3. **Load unpacked** → select this `extension/` folder
4. Click the toolbar icon and set:
   - **Poller URL** — e.g. `http://192.168.1.10:8787` or `http://127.0.0.1:8787`
   - **API token** — must match `api_token` in `poller/config.yaml`
5. Log into [makerworld.com](https://makerworld.com) (or refresh an already-open tab)
6. Click **Push now**, then **Refresh status**

## Behaviour

- On cookie change for `makerworld.com` → debounced push to `POST /auth/cookie`
- Every 30 minutes → re-check / push if hash changed
- Popup shows poller health (cookie age, last poll, panel reachability)

## Privacy

- Cookies leave your browser **only** toward the poller URL you configure (your LAN)
- No cloud backend
- Keep `api_token` private on your network

## Icons

Placeholder PNGs are generated as simple solid squares. Replace `icons/*.png` if you want a custom mark.
