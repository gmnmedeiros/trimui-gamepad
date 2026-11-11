#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_DIR="$ROOT/dist/USB Controller.pak"

bash -n "$ROOT/scripts/build.sh" "$ROOT/scripts/install.sh" "$ROOT/scripts/verify.sh"
sh -n "$ROOT/pak/launch.sh"

required=(
    "$PACKAGE_DIR/launch.sh"
    "$PACKAGE_DIR/trimui-gamepad"
    "$PACKAGE_DIR/bin/tg5040/minui-presenter"
    "$PACKAGE_DIR/MANIFEST.sha256"
)
for path in "${required[@]}"; do
    [[ -e "$path" ]] || {
        echo "ERROR: missing $path" >&2
        exit 1
    }
done

file "$PACKAGE_DIR/trimui-gamepad" | grep -q 'ARM aarch64'
file "$PACKAGE_DIR/bin/tg5040/minui-presenter" | grep -q 'ARM aarch64'
strings "$PACKAGE_DIR/bin/tg5040/minui-presenter" >"$ROOT/.build/presenter.strings"
grep -q 'gamepad-status-file' "$ROOT/.build/presenter.strings"

(
    cd "$PACKAGE_DIR"
    shasum -a 256 -c MANIFEST.sha256
)

echo 'Verification passed.'
