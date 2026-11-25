#!/usr/bin/env bash
# Re-render site/social-card.png, the 1200x630 link preview (og:image) that
# Teams, Slack, X and friends show for https://melnychuk.github.io/kolobok/.
# Needs a Chromium-family browser and network access for the Google Fonts.
# Run after ./scripts/build-site-wasm.sh when the face or copy changes.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
browser=${BROWSER:-$(command -v chromium chromium-browser google-chrome microsoft-edge 2>/dev/null | head -n1)}
[ -n "$browser" ] || { echo "No Chromium-family browser found (set BROWSER=...)" >&2; exit 1; }

port=${PORT:-8765}
python3 -m http.server "$port" --bind 127.0.0.1 --directory "$root" >/dev/null 2>&1 &
server=$!
profile=$(mktemp -d)
trap 'kill $server 2>/dev/null; rm -rf "$profile"' EXIT
sleep 0.5

"$browser" --headless=new --user-data-dir="$profile" --hide-scrollbars \
  --force-device-scale-factor=1 --window-size=1200,630 --virtual-time-budget=8000 \
  --screenshot="$root/site/social-card.png" \
  "http://127.0.0.1:$port/scripts/social-card.html" >/dev/null 2>&1
echo "Wrote site/social-card.png"
