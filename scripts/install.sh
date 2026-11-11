#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_DIR="$ROOT/dist/USB Controller.pak"
REMOTE_DIR="/mnt/SDCARD/Tools/tg5040/USB Controller.pak"
REMOTE_TMP="/mnt/SDCARD/Tools/tg5040/USB Controller.pak.tmp"

command -v adb >/dev/null 2>&1 || {
    echo "ERROR: adb is required" >&2
    exit 1
}

if [[ ! -x "$PACKAGE_DIR/trimui-gamepad" ]]; then
    "$ROOT/scripts/build.sh"
fi

ready_devices="$(adb devices | awk 'NR > 1 && $2 == "device" {count++} END {print count+0}')"
if [[ "$ready_devices" -ne 1 ]]; then
    echo "ERROR: expected exactly one ready ADB device; found $ready_devices" >&2
    exit 1
fi

adb shell 'test -d /mnt/SDCARD/Tools/tg5040' || {
    echo "ERROR: the NextUI tg5040 Tools directory is not available" >&2
    exit 1
}

adb shell "rm -rf '$REMOTE_TMP'; mkdir -p '$REMOTE_TMP'"
adb push "$PACKAGE_DIR/." "$REMOTE_TMP/"
adb shell "
    chmod 755 '$REMOTE_TMP/launch.sh' \
              '$REMOTE_TMP/trimui-gamepad' \
              '$REMOTE_TMP/bin/tg5040/minui-presenter' &&
    rm -rf '$REMOTE_DIR' &&
    mv '$REMOTE_TMP' '$REMOTE_DIR' &&
    sync
"

printf 'Installed: %s\n' "$REMOTE_DIR"
