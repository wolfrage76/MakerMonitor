#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export PATH="${HOME}/.local/bin:${PATH}"

echo "Looking for CrowPanel serial port..."
for i in $(seq 1 60); do
  for p in /dev/ttyACM* /dev/ttyUSB*; do
    if [[ -e "$p" ]]; then
      echo "Found $p — uploading..."
      exec pio run -t upload --upload-port "$p"
    fi
  done
  if (( i == 1 )); then
    echo "No /dev/ttyACM* or /dev/ttyUSB* yet."
    echo "Plug in the CrowPanel via USB-C (hold BOOT if needed), waiting up to 60s..."
  fi
  sleep 1
done

echo "Timed out — no serial device appeared."
echo "Tips:"
echo "  - Use a data USB-C cable (not charge-only)"
echo "  - Hold BOOT, tap RESET, release BOOT, then re-run"
echo "  - Check: lsusb | grep -i 303a"
exit 1
