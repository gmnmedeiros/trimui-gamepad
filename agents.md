# Agent Guide

## Project purpose

This repository builds a NextUI manual pak that turns a TRIMUI Smart Pro into a wired macOS and Steam controller.

The device runs Tina Linux 4.9.191. The stock kernel has ConfigFS and FunctionFS but does not include the ConfigFS HID function, so HID is implemented in userspace.

## Important files

- `src/trimui-gamepad.c` — evdev-to-FunctionFS bridge and Stadia-compatible HID reports.
- `pak/launch.sh` — USB gadget lifecycle, ADB restoration, and pak startup/cleanup.
- `presenter/minui-presenter.c` — patched visual controller interface.
- `docs/architecture.md` — protocol, mappings, lifecycle, and safety details.
- `docs/testing.md` — build, installation, macOS, Steam, and restoration checks.
- `scripts/build.sh` — reproducible Docker build and pak assembly.
- `scripts/install.sh` — ADB installation to the Smart Pro.
- `scripts/verify.sh` — artifact and manifest checks.

## Build and test

```bash
make package
make verify
```

Install only when one Smart Pro is connected in normal ADB mode:

```bash
make install
```

The generated pak is under:

```text
dist/USB Controller.pak
```

## Required safety rules

- Never expose the mounted SD card block device through USB mass storage.
- Never close FunctionFS ep0 before unbinding the UDC. This can crash the vendor USB driver.
- Keep the stop order documented in `docs/architecture.md`.
- Restore the original product ID, product string, and `ffs.adb` link on every normal exit.
- Do not install through ADB while controller mode is active because ADB is temporarily replaced.
- Do not add persistent firmware or kernel modifications for behavior that can remain inside the pak.
- Treat a 15-second Power hold as emergency recovery only; forced restarts can leave exFAT unclean.

## USB compatibility

Controller mode intentionally presents the wired Google Stadia identity `18d1:9400` and report format. Steam otherwise enumerates the unknown generic HID interface with `driver = NONE (DISABLED)`.

Do not change the USB identity or report layout without testing:

1. macOS `hidutil` and IORegistry detection.
2. Steam `console_log.txt` and `controller.txt`.
3. Every button, D-pad direction, trigger, and stick.
4. A+B hold-and-release exit.
5. Restoration of `TRIMUI ADB`, product ID `0xd002`, and `ffs.adb`.

## Input mapping constraints

The stock Smart Pro mapping is:

- Left stick: `ABS_X`, `ABS_Y`
- Right stick: `ABS_RX`, `ABS_RY`
- Triggers: `ABS_Z`, `ABS_RZ`, values 0 or 255
- D-pad: `ABS_HAT0X`, `ABS_HAT0Y`
- System buttons: `BTN_SELECT`, `BTN_START`, `BTN_MODE`

Do not substitute mappings from newer open-source Trimui Inputd implementations without checking raw events on the target firmware.

## Code style

- Use C11 for the bridge and keep it warning-free under `-Wall -Wextra -Werror`.
- Keep the bridge statically linked.
- Keep shell scripts compatible with BusyBox `ash` on the device.
- Use explicit cleanup paths and preserve idempotent recovery behavior.
- Keep runtime state under `/tmp`; avoid frequent writes to the SD card.
- Update documentation when USB protocol, mappings, build pins, or recovery behavior changes.

## Before pushing changes

```bash
make clean
make package
make verify
git diff --check
```

For device-facing changes, run the functional procedure in `docs/testing.md` before considering the work complete.
