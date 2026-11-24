#!/usr/bin/env bash
# Agent Herdr installer: the Herdr bridge for Linux or macOS, optional
# local dictation (Linux) and optional firmware flashing.
#
#   curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash
#   curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash -s -- --flash
#   curl -fsSL https://melnychuk.github.io/stick-s3/install.sh | bash -s -- --help
#
# Everything runs as your user. It never uses sudo, never uploads audio, and
# asks before replacing the Stick's firmware.
set -euo pipefail

REPO_URL="${STICK_S3_REPO_URL:-https://github.com/melnychuk/stick-s3.git}"
BASE_URL="${STICK_S3_BASE_URL:-https://melnychuk.github.io/stick-s3}"
DATA="${STICK_S3_DATA:-$HOME/.local/share/stick-s3}"
APP="${STICK_S3_DIR:-$DATA/app}"
MODEL="$DATA/asr/nemotron-en-int8"
UNIT_NAME="stick-s3-herdr.service"
MAC_LABEL="io.github.melnychuk.stick-s3"
SERIAL_DIR="${STICK_S3_SERIAL_DIR:-/dev/serial/by-id}"
ESPTOOL_VERSION="5.3.0"
SHERPA_VERSION="1.13.8"
ASR_REPO="csukuangfj/sherpa-onnx-nemotron-speech-streaming-en-0.6b-int8-2026-01-14"
ASR_REVISION="f13b0c6a48186fdd9fdd8d203b9527b0b709b09f"

usage() {
  cat <<'EOF'
Install the Agent Herdr bridge (and optionally flash the StickS3 firmware).

Usage: install.sh [options]

  --flash              Also flash the latest prebuilt firmware to the Stick.
  --flash-only         Only flash the firmware; do not install the bridge.
  --dictation          Linux: enable local push-to-talk dictation (CPU-only
                       speech recognition; downloads ~632 MiB of model files).
  --port PATH          Stick serial port (default: the single detected one).
  --helper PATH        Linux: Herdr task helper (herdr_tasks.py) to read.
  --agents-plugin ID   Linux: Omarchy Agents panel plugin used for dictation.
  --session NAME       macOS: Herdr session attached in Ghostty (default: default).
  --source DIR         Use this existing checkout instead of cloning/updating.
  --yes                Do not ask for confirmation (flashing).
  --uninstall          Stop and remove the bridge service (keeps downloads).
  -h, --help           Show this help.

Environment: STICK_S3_DIR (checkout, default ~/.local/share/stick-s3/app),
STICK_S3_HERDR_HELPER, STICK_S3_AGENTS_PLUGIN.
EOF
}

# Colours only on a terminal: agents and logs get plain text.
if [[ -t 2 && -z ${NO_COLOR:-} ]]; then B=$'\033[1;34m' Y=$'\033[1;33m' R=$'\033[1;31m' N=$'\033[0m'; else B="" Y="" R="" N=""; fi
say() { printf '%s==>%s %s\n' "$B" "$N" "$*"; }
note() { printf '    %s\n' "$*"; }
warn() { printf '%swarning:%s %s\n' "$Y" "$N" "$*" >&2; }
die() { printf '%serror:%s %s\n' "$R" "$N" "$*" >&2; exit 1; }

confirm() {
  [[ $YES == 1 ]] && return 0
  local answer=""
  if ! { exec 3</dev/tty; } 2>/dev/null; then
    die "$1 Re-run with --yes to confirm non-interactively."
  fi
  read -r -u 3 -p "$1 [y/N] " answer || true
  exec 3<&-
  [[ $answer == [yY]* ]]
}

need() { command -v "$1" >/dev/null 2>&1 || die "$1 is required but was not found. $2"; }

# ------------------------------------------------------------------ source
fetch_source() {
  if [[ -n $SOURCE ]]; then
    APP=$(cd "$SOURCE" && pwd)
    [[ -f $APP/bridge/herdr_bridge.py ]] || die "$APP is not a stick-s3 checkout."
    say "Using checkout $APP"
    return
  fi
  need git "Install git and re-run."
  if [[ -d $APP/.git ]]; then
    say "Updating $APP"
    git -C "$APP" pull --ff-only --quiet || die "Could not update $APP (local changes?)."
  elif [[ -e $APP ]]; then
    die "$APP exists but is not a git checkout; move it or set STICK_S3_DIR."
  else
    say "Downloading Agent Herdr to $APP"
    mkdir -p "$(dirname "$APP")"
    git clone --quiet --depth 1 "$REPO_URL" "$APP"
  fi
}

python_ok() {
  "$1" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null
}

make_venv() {
  local python=""
  for candidate in python3 python3.13 python3.12 python3.11 python3.10; do
    if command -v "$candidate" >/dev/null 2>&1 && python_ok "$candidate"; then python=$candidate; break; fi
  done
  [[ -n $python ]] || die "Python 3.10 or newer is required (macOS: brew install python)."
  if [[ ! -x $APP/.venv/bin/python ]]; then
    say "Creating Python environment"
    "$python" -m venv "$APP/.venv" || die "Could not create a virtual environment (Debian/Ubuntu: install python3-venv)."
  fi
  PY="$APP/.venv/bin/python"
}

pip_install() {
  [[ $# -gt 0 ]] || return 0
  say "Installing Python packages: $*"
  "$PY" -m pip install --quiet --disable-pip-version-check "$@"
}

# ------------------------------------------------------------- serial port
detect_port() {
  [[ -n $PORT ]] && { [[ -e $PORT ]] || die "Port $PORT does not exist."; return; }
  local -a ports=()
  if [[ $OS == Darwin ]]; then
    shopt -s nullglob; ports=(/dev/cu.usbmodem*); shopt -u nullglob
  else
    shopt -s nullglob; ports=("$SERIAL_DIR"/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00); shopt -u nullglob
  fi
  if [[ ${#ports[@]} -eq 1 ]]; then PORT=${ports[0]}; return; fi
  if [[ ${#ports[@]} -eq 0 ]]; then
    die "No StickS3 found. Connect it with a USB data cable (or pass --port)."
  fi
  die "Several serial devices found (${ports[*]}); pass --port to pick the Stick."
}

# ---------------------------------------------------------------- dictation
install_model() {
  need curl ""
  mkdir -p "$MODEL"
  say "Downloading the speech model to $MODEL (about 632 MiB, verified)"
  local hash name
  while read -r hash name; do
    if [[ -f $MODEL/$name ]] && [[ $(sha256 "$MODEL/$name") == "$hash" ]]; then
      note "$name already present"
      continue
    fi
    curl -fL --retry 2 --progress-bar \
      "https://huggingface.co/$ASR_REPO/resolve/$ASR_REVISION/$name" -o "$MODEL/$name.part"
    [[ $(sha256 "$MODEL/$name.part") == "$hash" ]] || { rm -f "$MODEL/$name.part"; die "Checksum mismatch for $name."; }
    mv "$MODEL/$name.part" "$MODEL/$name"
  done <<'FILES'
2f6ae81fe4ccd69ef04cdf048ecd49628e2d3148a6195e152a91b4d2497952dc encoder.int8.onnx
1fb1795cb46e7d0e99b2e096eae83f7e324294e895975a1a894b0384cbbe37f6 decoder.int8.onnx
a3f41dccc0f67f37e4210051d1c39a29d473c841cfc32fe574135bac890db91d joiner.int8.onnx
dc0b4584ab2e4ddbf888425c076c61b736e7356a015250db7d307e6f1a8188ff tokens.txt
FILES
}

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
  else shasum -a 256 "$1" | cut -d' ' -f1; fi
}

# ------------------------------------------------------------------- Linux
unit_quote() {  # systemd: escape specifiers, $ expansion and quotes
  local value=$1
  value=${value//\\/\\\\}; value=${value//\"/\\\"}; value=${value//%/%%}; value=${value//\$/\$\$}
  printf '"%s"' "$value"
}

find_helper() {
  HELPER=${HELPER:-${STICK_S3_HERDR_HELPER:-}}
  PLUGIN=${PLUGIN:-${STICK_S3_AGENTS_PLUGIN:-}}
  if [[ -z $HELPER ]]; then
    local -a found=()
    shopt -s nullglob; found=("$HOME"/.config/omarchy/plugins/*/herdr_tasks.py); shopt -u nullglob
    if [[ ${#found[@]} -eq 1 ]]; then
      HELPER=${found[0]}
    elif [[ ${#found[@]} -gt 1 ]]; then
      die "Several Herdr task helpers found; pass --helper: ${found[*]}"
    fi
  fi
  if [[ -n $HELPER ]]; then
    [[ -f $HELPER ]] || die "Herdr task helper not found: $HELPER"
    HELPER=$(cd "$(dirname "$HELPER")" && pwd)/$(basename "$HELPER")
    # The panel plugin that ships the helper also hosts the dictation popup.
    [[ -n $PLUGIN ]] || PLUGIN=$(basename "$(dirname "$HELPER")")
  fi
}

install_linux() {
  need systemctl "The Linux bridge runs as a systemd user service."
  find_helper
  local config="$HOME/.config/stick-s3/herdr.json" combined=0
  [[ -f $config ]] && combined=1
  if [[ -z $HELPER && $combined == 0 ]]; then
    die "No Herdr task helper found. Install the Omarchy Agents panel plugin that provides
       herdr_tasks.py (or pass --helper), or register native Herdr instances in
       $config (see docs/herdr-instances.example.json)."
  fi
  make_venv
  local -a packages=("numpy>=1.24")   # the voice worker needs numpy even when idle
  [[ $DICTATION == 1 ]] && packages+=("sherpa-onnx==$SHERPA_VERSION")
  pip_install "${packages[@]}"
  [[ $DICTATION == 1 ]] && install_model

  local -a exec=("$PY" "$APP/bridge/herdr_bridge.py")
  [[ $combined == 1 ]] && exec+=(--helper "$APP/bridge/combined_herdr.py")
  [[ -n $PORT ]] && exec+=(--port "$PORT")

  say "Checking Herdr (read-only)"
  local status
  if status=$(STICK_S3_HERDR_HELPER="$HELPER" "${exec[@]}" --once 2>&1); then
    note "Status now: ${status##*$'\n'}"
  else
    warn "The status check failed (${status##*$'\n'}). Is Herdr running? Installing anyway; check the logs below."
  fi

  local units="$HOME/.config/systemd/user" line="" part
  local unit="$units/$UNIT_NAME"
  mkdir -p "$units"
  [[ -f $unit ]] && cp "$unit" "$unit.bak.$(date +%s)"
  for part in "${exec[@]}"; do line+="$(unit_quote "$part") "; done
  {
    printf '[Unit]\nDescription=Agent Herdr USB bridge\n\n[Service]\nType=simple\n'
    [[ -n $HELPER ]] && printf 'Environment=%s\n' "$(unit_quote "STICK_S3_HERDR_HELPER=$HELPER")"
    [[ -n $PLUGIN ]] && printf 'Environment=%s\n' "$(unit_quote "STICK_S3_AGENTS_PLUGIN=$PLUGIN")"
    printf 'ExecStart=%s\nRestart=on-failure\nRestartSec=3\nNoNewPrivileges=true\n\n' "${line% }"
    printf '[Install]\nWantedBy=default.target\n'
  } > "$unit"
  if compgen -G "$units/$UNIT_NAME.d/*.conf" >/dev/null; then
    warn "Existing overrides in $units/$UNIT_NAME.d/ still apply."
  fi
  systemctl --user daemon-reload
  systemctl --user enable "$UNIT_NAME" >/dev/null 2>&1
  systemctl --user restart "$UNIT_NAME"
  say "Bridge installed and started ($UNIT_NAME)"
  [[ -n $HELPER ]] && note "Herdr helper: $HELPER"
  [[ $combined == 1 ]] && note "Combined CLI + native mode ($config)"
  if [[ $DICTATION == 1 ]]; then
    if [[ -n $PLUGIN ]]; then
      note "Dictation: hold the front key; text appears in the $PLUGIN popup."
    else
      warn "Dictation needs --agents-plugin (no Agents panel plugin was detected)."
    fi
  fi
  check_serial_access
  note "Logs: journalctl --user -u $UNIT_NAME -f"
}

check_serial_access() {
  local -a ports=()
  shopt -s nullglob; ports=("$SERIAL_DIR"/usb-Espressif_USB_JTAG_serial_debug_unit_*-if00); shopt -u nullglob
  [[ ${#ports[@]} -eq 0 ]] && { note "Plug in the Stick; the bridge connects automatically."; return; }
  local tty serial
  tty=$(readlink -f "${ports[0]}")
  if [[ -r $tty && -w $tty ]]; then note "Stick found at ${ports[0]}"; return; fi
  serial=$(udevadm info --query=property --name="$tty" 2>/dev/null | sed -n 's/^ID_SERIAL_SHORT=//p')
  warn "You cannot open $tty yet. Grant your desktop session access to this Stick only:"
  cat >&2 <<EOF
    echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="303a", ATTRS{idProduct}=="1001", ATTRS{serial}=="${serial:-YOUR_DEVICE_SERIAL}", TAG+="uaccess"' \\
      | sudo tee /etc/udev/rules.d/70-stick-s3.rules
    sudo udevadm control --reload-rules && sudo udevadm trigger --action=change "$tty"
    systemctl --user restart $UNIT_NAME
EOF
}

uninstall_linux() {
  systemctl --user disable --now "$UNIT_NAME" >/dev/null 2>&1 || true
  rm -f "$HOME/.config/systemd/user/$UNIT_NAME"
  systemctl --user daemon-reload || true
  say "Removed $UNIT_NAME (downloads in $DATA were kept)."
}

# ------------------------------------------------------------------- macOS
mac_plist() { echo "$HOME/Library/LaunchAgents/$MAC_LABEL.plist"; }

install_mac() {
  local herdr
  herdr=$(command -v herdr || true)
  [[ -n $herdr ]] || die "Herdr was not found in PATH."
  make_venv
  local -a command=("$PY" "$APP/bridge/macos_herdr_bridge.py" --herdr "$herdr" --session "$SESSION")
  [[ -n $PORT ]] && command+=(--port "$PORT")
  say "Checking Herdr session '$SESSION' (read-only)"
  "${command[@]}" --once || die "Status check failed: is Herdr running with session '$SESSION'? (--session NAME)"
  local logs="$HOME/Library/Logs/stick-s3" plist
  plist=$(mac_plist)
  mkdir -p "$logs" "$(dirname "$plist")"
  "$PY" - "$plist" "$MAC_LABEL" "$logs" "${command[@]}" <<'PY'
import plistlib, sys
plist, label, logs, *command = sys.argv[1:]
with open(plist, "wb") as f:
    plistlib.dump({"Label": label, "ProgramArguments": command, "RunAtLoad": True,
                   "KeepAlive": True, "ThrottleInterval": 10,
                   "StandardOutPath": f"{logs}/bridge.log",
                   "StandardErrorPath": f"{logs}/bridge-error.log"}, f)
PY
  launchctl bootout "gui/$(id -u)/$MAC_LABEL" >/dev/null 2>&1 || true
  launchctl bootstrap "gui/$(id -u)" "$plist"
  say "Bridge installed and started ($MAC_LABEL)"
  note "Logs: $logs/bridge{,-error}.log"
  note "Short-press the front key to open the next finished or waiting task."
}

uninstall_mac() {
  launchctl bootout "gui/$(id -u)/$MAC_LABEL" >/dev/null 2>&1 || true
  rm -f "$(mac_plist)"
  say "Removed $MAC_LABEL (downloads in $DATA were kept)."
}

# --------------------------------------------------------------- firmware
bridge_pause() {
  if [[ $OS == Darwin ]]; then
    if launchctl print "gui/$(id -u)/$MAC_LABEL" >/dev/null 2>&1; then
      launchctl bootout "gui/$(id -u)/$MAC_LABEL" >/dev/null 2>&1 || true
      PAUSED=mac
    fi
  elif command -v systemctl >/dev/null 2>&1 && systemctl --user is-active --quiet "$UNIT_NAME"; then
    systemctl --user stop "$UNIT_NAME"; PAUSED=linux
  fi
}

bridge_resume() {
  case ${PAUSED:-} in
    mac) launchctl bootstrap "gui/$(id -u)" "$(mac_plist)" >/dev/null 2>&1 || true ;;
    linux) systemctl --user start "$UNIT_NAME" || true ;;
  esac
  PAUSED=""
}

flash_firmware() {
  need curl ""
  make_venv
  pip_install "esptool==$ESPTOOL_VERSION"
  detect_port
  local dir
  dir=$(mktemp -d)
  say "Downloading firmware"
  curl -fsSL "$BASE_URL/firmware/manifest.json" -o "$dir/manifest.json" \
    || die "Could not download the firmware manifest from $BASE_URL/firmware/."
  # Download each part and verify its checksum; write "offset path" lines.
  # (Run as its own command: a failed check must stop the install.)
  local -a parts=()
  if "$PY" - "$dir" "$BASE_URL/firmware" > "$dir/parts" <<'PY'
import hashlib, json, sys, urllib.request
folder, base = sys.argv[1:]
manifest = json.load(open(f"{folder}/manifest.json"))
assert manifest.get("chip") == "esp32s3", "unexpected chip"
for part in manifest["files"]:
    name = part["name"]
    assert "/" not in name and not name.startswith("."), name
    path = f"{folder}/{name}"
    with urllib.request.urlopen(f"{base}/{name}") as response, open(path, "wb") as out:
        out.write(response.read())
    digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
    if digest != part["sha256"]:
        sys.exit(f"checksum mismatch for {name}")
    print(part["offset"])
    print(path)
PY
  then :; else die "Firmware download or verification failed."; fi
  mapfile -t parts < "$dir/parts"
  [[ ${#parts[@]} -ge 2 ]] || die "The firmware manifest lists no files."
  confirm "Flash the Agent Herdr firmware to $PORT? This replaces what is on it." \
    || die "Cancelled; nothing was flashed."
  bridge_pause
  trap bridge_resume EXIT
  say "Flashing $PORT (do not unplug)"
  "$PY" -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default-reset --after hard-reset write-flash -z \
    --flash-mode keep --flash-freq keep --flash-size keep "${parts[@]}" \
    || die "Flashing failed. If it will not start, hold the side power key ~2s until the green LED blinks, then retry."
  bridge_resume
  trap - EXIT
  rm -rf "$dir"
  say "Firmware flashed. The Stick restarts by itself."
}

# ------------------------------------------------------------------- main
main() {
  FLASH=0; FLASH_ONLY=0; DICTATION=0; YES=0; UNINSTALL=0
  PORT=""; HELPER=""; PLUGIN=""; SESSION="default"; SOURCE=""; PAUSED=""
  while [[ $# -gt 0 ]]; do
    case $1 in
      --flash) FLASH=1 ;;
      --flash-only) FLASH=1; FLASH_ONLY=1 ;;
      --dictation) DICTATION=1 ;;
      --port) PORT=${2:?--port needs a path}; shift ;;
      --helper) HELPER=${2:?--helper needs a path}; shift ;;
      --agents-plugin) PLUGIN=${2:?--agents-plugin needs an id}; shift ;;
      --session) SESSION=${2:?--session needs a name}; shift ;;
      --source) SOURCE=${2:?--source needs a directory}; shift ;;
      --yes|-y) YES=1 ;;
      --uninstall) UNINSTALL=1 ;;
      -h|--help) usage; return 0 ;;
      *) usage >&2; die "Unknown option: $1" ;;
    esac
    shift
  done
  OS=${STICK_S3_OS:-$(uname -s)}
  [[ $OS == Linux || $OS == Darwin ]] || die "Only Linux and macOS are supported."
  if [[ $UNINSTALL == 1 ]]; then
    if [[ $OS == Darwin ]]; then uninstall_mac; else uninstall_linux; fi
    return 0
  fi
  [[ $OS == Darwin && $DICTATION == 1 ]] && warn "Dictation is Linux-only; the macOS bridge handles status and focus."
  fetch_source
  if [[ $FLASH_ONLY == 0 ]]; then
    if [[ $OS == Darwin ]]; then install_mac; else install_linux; fi
  fi
  [[ $FLASH == 1 ]] && flash_firmware
  say "Done. More: $BASE_URL"
}

main "$@"
