#!/usr/bin/env bash
# Capture every color-panel page, Creator tab and Settings picker as PNGs.
#   tools/capture_screens.sh [panel-host] [out-dir]
# Defaults: makermonitor.local, docs/screens. If the web console has a
# password, set MM_AUTH=admin:<password>.
set -euo pipefail
HOST="${1:-makermonitor.local}"
OUT="${2:-$(dirname "$0")/../docs/screens}"
WAIT="${WAIT:-1.5}"
mkdir -p "$OUT"
AUTH=()
[ -n "${MM_AUTH:-}" ] && AUTH=(-u "$MM_AUTH")

shot() {  # name, then query string for /v2/ui/show
  local name="$1" query="$2"
  if ! curl -sf "${AUTH[@]}" -X POST "http://$HOST/v2/ui/show?$query" >/dev/null; then
    echo "skip  $name (show failed)"
    return
  fi
  sleep "$WAIT"
  curl -sf "${AUTH[@]}" -o "$OUT/$name.png" "http://$HOST/screenshot.png" && echo "saved $OUT/$name.png"
}

for p in today feed models trend points reach comments best status settings about update; do
  shot "$p" "page=$p"
done
for t in convert trend launch earn followers reply; do shot "creator-$t" "page=creator&tab=$t"; done
shot "settings-alerts" "page=settings&open=alerts"
shot "settings-device" "page=settings&open=device"
shot "settings-brightness" "page=settings&open=bright"
shot "settings-reset" "page=settings&open=reset"
shot "settings-quiet-from" "page=settings&open=quiet_from"
shot "settings-quiet-until" "page=settings&open=quiet_until"
shot "settings-notify" "page=settings&open=notify"
shot "settings-poll" "page=settings&open=poll"
# First model in the lists, if any.
id=$(curl -sf "${AUTH[@]}" "http://$HOST/v2/ui/pages" | python3 -c 'import json,sys;m=json.load(sys.stdin)["open"]["models"][1:];print(m[0] if m else "")' || true)
[ -n "$id" ] && shot "model-detail" "page=models&open=$id"
shot "guide" "page=guide"
curl -sf "${AUTH[@]}" -X POST "http://$HOST/v2/ui/show?page=today" >/dev/null || true
