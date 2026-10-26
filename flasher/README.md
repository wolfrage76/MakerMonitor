# Browser flasher

`index.html` is a one-page installer built on [ESP Web Tools](https://esphome.github.io/esp-web-tools/): plug a color panel in over USB, open the page in Chrome or Edge, pick the board and press **Connect and install**. It writes a full image (bootloader, partition table, OTA data and app) at offset 0, so it works on a blank or crash-looping panel. After that, updates go over Wi-Fi.

Build the files to publish:

```bash
tools/build_flasher.sh            # HEAD, crowpanel_70
tools/build_flasher.sh v0.4.2     # a tag or commit
```

It builds from a clean export of the given commit (never your uncommitted work), refuses an image whose display interrupt code isn't in IRAM, and writes `flasher/dist/` (gitignored): `index.html`, `boards.json`, one `manifest-<env>.json` per board and the merged images under `firmware/`. Don't run it while another firmware build is going: pioarduino rebuilds one shared Arduino core for every project.

Try it locally before publishing (Web Serial only works over https or on localhost):

```bash
python3 -m http.server -d flasher/dist 8000
```

then open http://localhost:8000 in Chrome. Publish by uploading everything in `flasher/dist/` to any https host; the page and the images must be on the same site.
