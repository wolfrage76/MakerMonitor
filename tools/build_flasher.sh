#!/usr/bin/env bash
# Builds the browser flasher (flasher/dist/): one full first-flash image per
# board, an ESP Web Tools manifest for each, and boards.json for the page.
#
#   tools/build_flasher.sh [git-ref] [env ...]
#
# Builds from a clean export of git-ref (default HEAD), so uncommitted work in
# firmware-color/ never ends up in a published image. Default envs: crowpanel_70 waveshare_70 jc3248w535 ws_lcd2.
# Each board's OTA app image (the asset name from its board header) also goes
# to flasher/release/ for the GitHub release.
#
# pioarduino rebuilds one shared Arduino core (~/.platformio/packages/
# framework-arduinoespressif32-libs) for whichever custom_sdkconfig built last.
# Don't run this while another firmware build is going, and expect the next
# build in firmware-color/ to rebuild the core again (delete its
# sdkconfig.defaults first, as the firmware README says).
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
REF="${1:-HEAD}"
shift || true
ENVS=("$@")
[ ${#ENVS[@]} -eq 0 ] && ENVS=(crowpanel_70 waveshare_70 jc3248w535 ws_lcd2)

PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
ESPTOOL="${ESPTOOL:-$HOME/.platformio/penv/bin/esptool.py}"
NM="$HOME/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm"
DIST="$REPO/flasher/dist"

board_name() {
  case "$1" in
    crowpanel_43) echo 'Elecrow CrowPanel 4.3"' ;;
    crowpanel_50) echo 'Elecrow CrowPanel 5.0"' ;;
    crowpanel_70) echo 'Elecrow CrowPanel 7.0"' ;;
    waveshare_70) echo 'Waveshare ESP32-S3-Touch-LCD-7' ;;
    jc3248w535) echo 'Guition JC3248W535 3.5"' ;;
    ws_lcd2) echo 'Waveshare ESP32-S3-Touch-LCD-2' ;;
    *) echo "$1" ;;
  esac
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
git -C "$REPO" archive "$REF" firmware-color | tar -x -C "$WORK"
SRC="$WORK/firmware-color"
VERSION="$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' "$SRC/include/fw_version.h")"
COMMIT="$(git -C "$REPO" rev-parse --short "$REF")"
echo "Building v$VERSION ($COMMIT) for ${ENVS[*]}"

RELEASE="$REPO/flasher/release"
rm -rf "$DIST" "$RELEASE"
mkdir -p "$DIST/firmware" "$RELEASE"
cp "$REPO/flasher/index.html" "$DIST/"
boards_json=""

for env in "${ENVS[@]}"; do
  # A stale hash here links the core built for another env.
  rm -f "$SRC/sdkconfig.defaults" "$SRC/sdkconfig.$env"
  "$PIO" run -d "$SRC" -e "$env"
  build="$SRC/.pio/build/$env"

  # A build linked against the stock core crashes on its first flash write.
  # The Guition's QSPI and the Waveshare 2"'s SPI panels have no RGB ISR; the
  # Guition's speaker I2S one must be in IRAM.
  want=(gdma_start gdma_reset gdma_append gdma_hal_append gdma_default_tx_isr)
  case "$env" in jc3248w535|ws_lcd2) ;; *) want+=(rgb_lcd_default_isr_handler) ;; esac
  case "$env" in crowpanel_70|jc3248w535) want+=(i2s_dma_tx_callback) ;; esac
  syms="$("$NM" "$build/firmware.elf")"
  for s in "${want[@]}"; do
    if ! grep -qE "^403[0-9a-f]{5} [tT] $s\$" <<<"$syms"; then
      echo "error: $s is not in IRAM in the $env image; not publishing it" >&2
      exit 1
    fi
  done

  boot_app0="$(find "$HOME/.platformio/packages" -path '*arduinoespressif32*/tools/partitions/boot_app0.bin' | head -1)"
  image="makermonitor-${env//_/-}-$VERSION.bin"
  "$ESPTOOL" --chip esp32s3 merge_bin -o "$DIST/firmware/$image" \
    --flash_mode keep --flash_freq keep --flash_size keep \
    0x0 "$build/bootloader.bin" \
    0x8000 "$build/partitions.bin" \
    0xe000 "$boot_app0" \
    0x10000 "$build/firmware.bin"

  asset="$(sed -n 's/^#define BOARD_OTA_ASSET *"\(.*\)"/\1/p' "$SRC/include/boards/$env.h")"
  [ -n "$asset" ] && cp "$build/firmware.bin" "$RELEASE/$asset"

  name="$(board_name "$env")"
  name="${name//\"/\\\"}"
  cat >"$DIST/manifest-$env.json" <<EOF
{
  "name": "MakerMonitor for the $name",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [{ "path": "firmware/$image", "offset": 0 }] }
  ]
}
EOF
  boards_json+="${boards_json:+,}
    { \"env\": \"$env\", \"name\": \"$name\", \"manifest\": \"manifest-$env.json\" }"
done

cat >"$DIST/boards.json" <<EOF
{
  "version": "$VERSION",
  "commit": "$COMMIT",
  "boards": [$boards_json
  ]
}
EOF
echo "Done: $DIST (serve it with: python3 -m http.server -d $DIST 8000)"
