#!/bin/sh
# First run: write /config/config.yaml from the example, with no panels, the
# handle from MM_HANDLE (blank if unset) and a random api_token. Later runs leave it alone.
set -e
CFG=/config/config.yaml
if [ ! -f "$CFG" ]; then
  if ! touch "$CFG" 2>/dev/null; then
    echo "Can't write $CFG. The mounted folder must be writable by uid $(id -u)." >&2
    echo "Create it before starting: mkdir -p docker-data (or set MM_UID/MM_GID)." >&2
    exit 1
  fi
  TOKEN=${MM_API_TOKEN:-$(python -c 'import secrets; print(secrets.token_urlsafe(18))')}
  sed -e "s|^handle:.*|handle: \"${MM_HANDLE}\"|" \
      -e 's|^panels:.*|panels: []|' \
      -e "s|^api_token:.*|api_token: \"$TOKEN\"|" \
      /app/config.example.yaml > "$CFG"
  echo "Created $CFG (handle: ${MM_HANDLE:-none yet, set it in the dashboard Settings})."
  echo "Dashboard: http://<this-host>:8787/?token=$TOKEN"
  echo "The token is also api_token in config.yaml; the browser extension needs it too."
fi
exec "$@"
