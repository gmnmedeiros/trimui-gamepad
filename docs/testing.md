# Testing

## Build validation

```bash
make clean
make package
make verify
```

`make verify` checks:

- Shell syntax.
- Required pak files.
- AArch64 architecture for both binaries.
- Presence of the patched presenter option.
- The package SHA-256 manifest.

## Installation validation

With one Smart Pro in normal ADB mode:

```bash
make install
adb shell 'find "/mnt/SDCARD/Tools/tg5040/USB Controller.pak" -maxdepth 3 -type f -print'
```

The pak must appear in **NextUI → Tools**.

## Functional test

1. Start USB Controller.
2. Confirm the visual layout renders.
3. Press A/B/X/Y and verify the corresponding circles highlight.
4. Press each D-pad direction and diagonal.
5. Test L1/R1 and L2/R2.
6. Test SELECT, START, and MENU.
7. Move each stick through its full range and verify the correct knob follows it.
8. Hold A+B for four seconds.
9. Release A+B and verify the pak exits once and stays closed.
10. Confirm ADB returns.

## macOS HID checks

While controller mode is active:

```bash
hidutil list | grep -i stadia
ioreg -p IOUSB -t | grep -i stadia
```

Expected identity:

```text
VID 0x18d1
PID 0x9400
Stadia Controller
```

## Steam checks

Keep Steam open before controller mode starts. Then inspect:

```bash
rg -n -i '18d1|9400|stadia' \
  "$HOME/Library/Application Support/Steam/logs/console_log.txt" | tail

tail -50 \
  "$HOME/Library/Application Support/Steam/logs/controller.txt"
```

Verified output on 2026-11-11:

```text
Added HIDAPI device 'Google Stadia Controller' VID 0x18d1, PID 0x9400
Driver = SDL_JOYSTICK_HIDAPI_STADIA (ENABLED)
Local Device Found type: 18d1 9400
Steam Controller reserving XInput slot 0
Controller using HIDAPI driver, vid=0x18d1, pid=0x9400
Controller PollState Changed from 0 to 1
```

Also use Steam's controller input test and verify buttons, D-pad, triggers, and both sticks.

## Exit and restoration checks

After normal exit:

```bash
adb wait-for-device
adb shell '
  cat /sys/kernel/config/usb_gadget/g1/strings/0x409/product
  cat /sys/kernel/config/usb_gadget/g1/idProduct
  ls -l /sys/kernel/config/usb_gadget/g1/configs/c.1
'
```

Expected:

- Product string is `TRIMUI ADB`.
- Product ID is the original ADB value.
- `ffs.adb` is linked.
- `ffs.gamepad` is absent.

## Known limitations

- Rumble is acknowledged but not implemented.
- The SD WebDAV mount pauses while ADB is replaced.
- L2/R2 are physically reported as digital 0/255 axes, so Steam receives digital trigger values.
- USB-C-to-C cables can fail to enter device mode on this hardware. Use USB-A-to-C.
