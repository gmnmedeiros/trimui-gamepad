# TRIMUI Gamepad

Turn a TRIMUI Smart Pro running NextUI into a wired USB controller for macOS and Steam.

The project installs a NextUI manual pak named **USB Controller**. It reads the handheld controls from Linux evdev, exports them through USB FunctionFS, and shows a responsive visual controller layout on the handheld screen.

## Features

- Works as a native macOS USB game controller.
- Works with Steam Input on macOS.
- Supports A/B/X/Y, D-pad, L1/R1, L2/R2, SELECT, START, MENU, and both analog sticks.
- Shows a live visual controller layout on the Smart Pro.
- Restores the normal ADB USB configuration when controller mode exits.
- Does not replace NextUI, modify the kernel, or expose the SD card as USB mass storage.

## Use

1. Connect a USB-A-to-C data cable to the Smart Pro's **bottom charging/data port**. This hardware does not reliably enter USB device mode with C-to-C cables.
2. In NextUI, open **Tools → USB Controller**.
3. Use the controller in Steam or another macOS application.
4. To exit, hold **A+B for four seconds**, then release both buttons.

The pak waits for A and B to be released before returning to NextUI. This prevents the held A button from immediately reopening the pak.

## Steam compatibility

Steam sees an unknown generic HID gamepad but leaves its driver disabled. To avoid a Mac-side custom driver or per-user Steam setting, controller mode presents the HID interface as a wired **Google Stadia Controller** (`18d1:9400`) and sends the documented Stadia USB report format.

Steam then selects its enabled `SDL_JOYSTICK_HIDAPI_STADIA` driver. This was verified in Steam's logs and with live inputs:

```text
Added HIDAPI device 'Google Stadia Controller' VID 0x18d1, PID 0x9400
Driver = SDL_JOYSTICK_HIDAPI_STADIA (ENABLED)
Steam Controller reserving XInput slot 0
```

The normal ADB identity is restored when the pak exits.

## Build

Requirements:

- macOS with Docker running
- `make`
- Network access for the first build

Build and verify the pak:

```bash
make package
make verify
```

The output is:

```text
dist/USB Controller.pak/
```

The build uses pinned ARM64 Docker images, NextUI `v6.15.0`, and a pinned Parson commit.

## Install

Install to one connected Smart Pro through ADB:

```bash
make install
```

The installer writes:

```text
/mnt/SDCARD/Tools/tg5040/USB Controller.pak
```

Do not run the installer while USB Controller mode is active because that mode temporarily replaces ADB.

## USB and SD-card behavior

The vendor USB device controller cannot safely bind the stock ADB FunctionFS interface and the gamepad FunctionFS interface together. Controller mode therefore:

1. Unbinds the USB device controller.
2. Temporarily removes the ADB function.
3. Adds the gamepad function and Stadia USB identity.
4. Rebinds USB.
5. Reverses these steps on exit.

The WebDAV SD mount pauses while controller mode is active because it depends on ADB. It can be mounted at `~/Volumes/TRIMUI` again after exit.

## Repository layout

```text
src/trimui-gamepad.c       FunctionFS HID and evdev bridge
presenter/                 Patched NextUI presenter and its MIT license
pak/launch.sh              Safe USB lifecycle and NextUI launcher
pak/README.md              README included in the device pak
scripts/build.sh           Reproducible ARM64 build and package assembly
scripts/install.sh         ADB installer
scripts/verify.sh          Artifact and manifest checks
docs/architecture.md       Detailed design and safety constraints
docs/testing.md            Verified behavior and test procedure
```

## Recovery

All USB gadget changes are temporary. If USB does not restore, reboot the Smart Pro. A forced restart requires holding Power for approximately 15 seconds, but use it only as recovery because forced restarts can leave the exFAT SD volume unclean.

## Licensing

The patched `minui-presenter` source retains its upstream MIT license in `presenter/LICENSE`. No project-wide license has been selected for the original bridge and scripts.
