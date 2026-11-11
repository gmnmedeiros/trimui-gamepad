#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/.build"
PRESENTER_BUILD="$BUILD_DIR/presenter"
PACKAGE_DIR="$ROOT/dist/USB Controller.pak"

BRIDGE_IMAGE="alpine:3.20@sha256:d9e853e87e55526f6b2917df91a2115c36dd7c696a35be12163d44e6e2a4b6bc"
PRESENTER_IMAGE="savant/minui-toolchain:tg5040-nextui@sha256:0df09bc4ac065a3eda0b8ed9893adef1263cec5145751c424d7a035c56b0fc64"

command -v docker >/dev/null 2>&1 || {
    echo "ERROR: docker is required" >&2
    exit 1
}

rm -rf "$BUILD_DIR" "$ROOT/dist"
mkdir -p "$BUILD_DIR" "$PRESENTER_BUILD" "$PACKAGE_DIR/bin/tg5040"

printf '%s\n' '[1/3] Building the static AArch64 FunctionFS bridge'
docker run --rm --platform linux/arm64 \
    -v "$ROOT:/workspace" -w /workspace \
    "$BRIDGE_IMAGE" sh -lc '
        set -e
        apk add --no-cache build-base linux-headers >/dev/null
        cc -std=c11 -O2 -Wall -Wextra -Werror -static -s \
            -o .build/trimui-gamepad src/trimui-gamepad.c
    '

printf '%s\n' '[2/3] Building the patched NextUI presenter'
cp "$ROOT/presenter/Makefile" \
   "$ROOT/presenter/minui-presenter.c" \
   "$ROOT/presenter/presenter_theme.c" \
   "$ROOT/presenter/presenter_theme.h" \
   "$PRESENTER_BUILD/"

docker run --rm --platform linux/arm64 \
    -v "$PRESENTER_BUILD:/root/workspace/repo" \
    -w /root/workspace/repo \
    -e PLATFORM=tg5040-nextui \
    "$PRESENTER_IMAGE" bash -lc '
        set -e
        source /root/.bashrc
        make setup
        make -j4
        aarch64-nextui-linux-gnu-strip minui-presenter-tg5040-nextui
    '

printf '%s\n' '[3/3] Assembling the NextUI manual pak'
install -m 0755 "$ROOT/pak/launch.sh" "$PACKAGE_DIR/launch.sh"
install -m 0755 "$BUILD_DIR/trimui-gamepad" "$PACKAGE_DIR/trimui-gamepad"
install -m 0755 "$PRESENTER_BUILD/minui-presenter-tg5040-nextui" \
    "$PACKAGE_DIR/bin/tg5040/minui-presenter"
install -m 0644 "$ROOT/pak/README.md" "$PACKAGE_DIR/README.md"
install -m 0644 "$ROOT/src/trimui-gamepad.c" "$PACKAGE_DIR/trimui-gamepad.c"
install -m 0644 "$ROOT/presenter/minui-presenter.c" "$PACKAGE_DIR/minui-presenter-live.c"
install -m 0644 "$ROOT/presenter/LICENSE" "$PACKAGE_DIR/LICENSE.minui-presenter"

(
    cd "$PACKAGE_DIR"
    find . -type f ! -name MANIFEST.sha256 -print0 \
        | sort -z \
        | xargs -0 shasum -a 256 >MANIFEST.sha256
)

printf 'Package: %s\n' "$PACKAGE_DIR"
