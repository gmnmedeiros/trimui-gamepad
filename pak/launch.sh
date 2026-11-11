#!/bin/sh

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR" .pak)"
GADGET="/sys/kernel/config/usb_gadget/g1"
FUNCTION="$GADGET/functions/ffs.gamepad"
LINK="$GADGET/configs/c.1/ffs.gamepad"
ADB_LINK="$GADGET/configs/c.1/ffs.adb"
ADB_FUNCTION="$GADGET/functions/ffs.adb"
PRODUCT_FILE="$GADGET/strings/0x409/product"
PRODUCT_ID_FILE="$GADGET/idProduct"
MOUNT_POINT="/dev/ffs-gamepad"
READY_FILE="/tmp/trimui-gamepad-ffs-ready"
STOP_FILE="/tmp/trimui-gamepad-stop"
STATUS_FILE="/tmp/trimui-gamepad-status"
PID_FILE="/tmp/trimui-gamepad.pid"
LOG_FILE="${LOGS_PATH:-/tmp}/$PAK_NAME.txt"
DAEMON_PID=""
PRESENTER_PID=""
UDC=""
ORIGINAL_PRODUCT=""
ORIGINAL_PRODUCT_ID=""
GADGET_WAS_BOUND=0
ADB_REMOVED=0
STARTED=0

export PATH="$PAK_DIR/bin/${PLATFORM:-tg5040}:$PATH"
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/mnt/SDCARD/.system/tg5040/lib:/usr/trimui/lib}"

rm -f "$LOG_FILE"
exec >>"$LOG_FILE" 2>&1
[ -f "$PAK_DIR/debug" ] && set -x

echo "Starting $PAK_NAME"
echo "1" >/tmp/stay_awake

show_message() {
    killall minui-presenter >/dev/null 2>&1 || true
    minui-presenter --message "$1" --timeout "${2:--1}" &
    PRESENTER_PID=$!
}

bind_gadget() {
    [ -n "$UDC" ] || return 1
    # This vendor kernel can return an error after it has bound the UDC.
    printf '%s' "$UDC" >"$GADGET/UDC" 2>/dev/null || true
    [ -s "$GADGET/UDC" ]
}

unbind_gadget() {
    if [ -s "$GADGET/UDC" ]; then
        # A newline causes a real sysfs write; a zero-byte printf does not.
        printf '\n' >"$GADGET/UDC"
    fi
}

cleanup() {
    trap - EXIT INT TERM HUP QUIT
    echo "Stopping $PAK_NAME"

    [ -n "$PRESENTER_PID" ] && kill "$PRESENTER_PID" >/dev/null 2>&1 || true
    killall minui-presenter >/dev/null 2>&1 || true

    # The FunctionFS process must keep ep0 open until the UDC is unbound.
    # Closing it first can crash this vendor USB driver.
    if [ "$STARTED" -eq 1 ]; then
        unbind_gadget
    fi

    if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" >/dev/null 2>&1; then
        kill "$DAEMON_PID" >/dev/null 2>&1 || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi

    if [ "$STARTED" -eq 1 ]; then
        rm -f "$LINK"
        if [ -n "$ORIGINAL_PRODUCT" ] && [ -f "$PRODUCT_FILE" ]; then
            printf '%s' "$ORIGINAL_PRODUCT" >"$PRODUCT_FILE"
        fi
        if [ -n "$ORIGINAL_PRODUCT_ID" ] && [ -f "$PRODUCT_ID_FILE" ]; then
            printf '%s' "$ORIGINAL_PRODUCT_ID" >"$PRODUCT_ID_FILE"
        fi
        if [ "$ADB_REMOVED" -eq 1 ] && [ -d "$ADB_FUNCTION" ]; then
            ln -s "$ADB_FUNCTION" "$ADB_LINK" 2>/dev/null || true
        fi
        umount "$MOUNT_POINT" >/dev/null 2>&1 || true
        rmdir "$MOUNT_POINT" >/dev/null 2>&1 || true
        rmdir "$FUNCTION" >/dev/null 2>&1 || true
        rm -f "$READY_FILE" "$STOP_FILE" "$STATUS_FILE" "$PID_FILE"

        if [ "$GADGET_WAS_BOUND" -eq 1 ]; then
            bind_gadget || echo "WARNING: Could not restore the USB gadget. Reboot the TRIMUI."
        fi
    fi
    rm -f /tmp/stay_awake
}
trap cleanup EXIT INT TERM HUP QUIT

if [ ! -x "$PAK_DIR/trimui-gamepad" ]; then
    show_message "USB Controller error: program is missing" 3
    exit 1
fi
if [ ! -d "$GADGET/configs/c.1" ]; then
    show_message "USB Controller error: USB gadget is unavailable" 3
    exit 1
fi
if [ -e "$LINK" ] || [ -d "$FUNCTION" ] || mount | grep -q "on $MOUNT_POINT "; then
    show_message "USB Controller was not stopped cleanly. Reboot the TRIMUI, then try again." 5
    exit 1
fi

UDC="$(cat "$GADGET/UDC" 2>/dev/null)"
if [ -n "$UDC" ]; then
    GADGET_WAS_BOUND=1
else
    UDC="$(ls /sys/class/udc 2>/dev/null | head -n 1)"
fi
if [ -z "$UDC" ]; then
    show_message "USB Controller error: no USB device controller" 3
    exit 1
fi
ORIGINAL_PRODUCT="$(cat "$PRODUCT_FILE" 2>/dev/null)"
ORIGINAL_PRODUCT_ID="$(cat "$PRODUCT_ID_FILE" 2>/dev/null)"

rm -f "$READY_FILE" "$STOP_FILE" "$STATUS_FILE" "$PID_FILE"
mkdir "$FUNCTION" || exit 1
STARTED=1
mkdir -p "$MOUNT_POINT" || exit 1
mount -t functionfs gamepad "$MOUNT_POINT" || exit 1

"$PAK_DIR/trimui-gamepad" &
DAEMON_PID=$!
echo "$DAEMON_PID" >"$PID_FILE"

attempt=0
while [ ! -f "$READY_FILE" ] && kill -0 "$DAEMON_PID" >/dev/null 2>&1 && [ "$attempt" -lt 50 ]; do
    sleep 0.1
    attempt=$((attempt + 1))
done
if [ ! -f "$READY_FILE" ]; then
    show_message "USB Controller could not start. See the log." 4
    exit 1
fi

# The vendor UDC cannot bind ADB and this HID function together. Controller
# mode therefore replaces ADB temporarily, then cleanup restores it.
unbind_gadget || exit 1
if [ -L "$ADB_LINK" ]; then
    rm -f "$ADB_LINK" || exit 1
    ADB_REMOVED=1
fi
[ -f "$PRODUCT_FILE" ] && printf '%s' "Stadia Controller" >"$PRODUCT_FILE"
[ -f "$PRODUCT_ID_FILE" ] && printf '%s' "0x9400" >"$PRODUCT_ID_FILE"
ln -s "$FUNCTION" "$LINK" || exit 1
if ! bind_gadget; then
    show_message "USB Controller could not connect. Reboot the TRIMUI." 5
    exit 1
fi

minui-presenter --message "USB CONTROLLER ACTIVE" --gamepad-status-file "$STATUS_FILE" --timeout -1 &
PRESENTER_PID=$!

while kill -0 "$DAEMON_PID" >/dev/null 2>&1 && [ ! -f "$STOP_FILE" ]; do
    sleep 0.1
done
if [ ! -f "$STOP_FILE" ]; then
    show_message "USB Controller stopped unexpectedly. Reboot if USB does not return." 4
    exit 1
fi

show_message "Stopping USB Controller..." -1
exit 0
