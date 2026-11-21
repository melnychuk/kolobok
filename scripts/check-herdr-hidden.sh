#!/bin/bash
# Check that the Stick widget stops all work while Herdr's window is hidden.
#
# Starts a separate Herdr (its own temporary home, core session and config, so
# yours is untouched), installs the widget from this checkout, then minimizes
# the window and counts renderer workers and CPU time. A Herdr window opens
# while it runs. Needs a Herdr with window().visible (herdrdev/herdr-app#1320);
# older releases keep the widget running, which this reports as a failure.
#
#     scripts/check-herdr-hidden.sh [path/to/herdr-app]
set -u
APP=${1:-herdr-app}
REPO=$(cd "$(dirname "$0")/.." && pwd)
root=$(mktemp -d /var/tmp/stick-hidden-XXXX)
export HERDR_APP_HOME=$root/home HC_SESSION=sh${root: -4} XDG_STATE_HOME=$root/state \
  XDG_CONFIG_HOME=$root/config HERDR_APP_NO_SOUND=1
unset HC_SOCKET HC_TERMINAL_ID HC_TERMINAL_SOCKET HC_SCOPE
WORKER="$HERDR_APP_HOME/mods/stick-s3-avatar/renderer.mjs"
mkdir -p "$HERDR_APP_HOME"
echo '{"frame.welcomed": true, "layout": "main"}' > "$HERDR_APP_HOME/shared.json"

workers() { pgrep -f "$WORKER --worker" | wc -l | tr -d ' '; }
# CPU seconds used so far by Herdr and the widget's renderer.
cpu() {
  ps -A -o pid=,time=,command= | awk -v p="$pid" -v w="$WORKER" \
    '($1 == p || index($0, w)) { split($2, t, ":"); s += t[1] * 60 + t[2] } END { printf "%.2f", s }'
}
# A fresh window takes a moment to read its layout and refuses until then.
run() {
  local out
  for _ in $(seq 50); do
    out=$("$APP" "do" "$@" 2>&1) && [[ $out != *error* ]] && return 0
    sleep 0.2
  done
  echo "do $* failed: $out" >&2
  exit 1
}
cleanup() {
  [[ -n ${pid:-} ]] && kill "$pid" 2>/dev/null && wait "$pid" 2>/dev/null
  hc server stop >/dev/null 2>&1
  pkill -f "$WORKER" 2>/dev/null
  rm -rf "$root"
}
trap cleanup EXIT

hc server start >/dev/null 2>&1
"$APP" > "$root/app.log" 2>&1 &
pid=$!
caffeinate -d -w "$pid" &  # A sleeping display hides the window too.
for _ in $(seq 100); do "$APP" status >/dev/null 2>&1 && break; sleep 0.2; done
# The installer needs the default layout, which the app writes on its first start.
for _ in $(seq 25); do
  python3 "$REPO/scripts/install-herdr-avatar.py" --app-home "$HERDR_APP_HOME" >/dev/null 2>&1 && break
  sleep 0.4
done
run layout.switch '{"name":"stick-s3"}'
sleep 4
shown=$(workers)
# window.minimize, not app.hide: an unbundled build doesn't hide.
run window.minimize
sleep 2
c0=$(cpu); sleep 10; c1=$(cpu)
hidden=$(workers)  # Also past the worker's 5 s lease: nothing came back by itself.
"$APP" >/dev/null 2>&1 &  # One Herdr per home: a second launch brings the window back.
sleep 4
again=$(workers)
echo "workers: shown $shown, minimized $hidden, shown again $again"
echo "CPU while minimized: $(echo "$c1 - $c0" | bc) s over 10 s (Herdr and renderer)"
[[ $shown == 1 && $hidden == 0 && $again == 1 ]] || { echo "FAIL" >&2; exit 1; }
echo "ok"
