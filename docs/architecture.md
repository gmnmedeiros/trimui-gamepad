# Architecture

## Device environment

The target is a TRIMUI Smart Pro (`tg5040`) running NextUI on Tina Linux 4.9.191. The built-in controls are provided by `trimui_inputd` as:

```text
/dev/input/event3: TRIMUI Player1
```

The stock USB gadget is ConfigFS gadget `g1` with an ADB FunctionFS function:

```text
/sys/kernel/config/usb_gadget/g1/functions/ffs.adb
/sys/kernel/config/usb_gadget/g1/configs/c.1/ffs.adb
```

Relevant kernel configuration:

```text
CONFIG_USB_GADGET=y
CONFIG_USB_CONFIGFS=y
CONFIG_USB_CONFIGFS_F_FS=y
# CONFIG_USB_CONFIGFS_F_HID is not set
```

A normal ConfigFS `hid.*` function is not available. The bridge implements HID in userspace through FunctionFS instead.

## Components

### `trimui-gamepad`

`src/trimui-gamepad.c` is a static AArch64 binary. It:

- Mounts through the `ffs.gamepad` FunctionFS instance created by `launch.sh`.
- Writes the HID interface, HID descriptor, interrupt-IN endpoint, strings, and report descriptor to ep0.
- Handles HID control requests through ep0.
- Reads `TRIMUI Player1` without taking an exclusive input grab.
- Rate-limits USB input writes to avoid analog-event bursts starving later controls.
- Writes the visual interface state atomically under `/tmp`.
- Arms exit after A+B remains held for four seconds and requests exit only after both are released.

FunctionFS uses virtual endpoint addresses. The interrupt-IN file is:

```text
/dev/ffs-gamepad/ep81
```

### Patched `minui-presenter`

The presenter is based on `josegonzalez/minui-presenter` and retains its MIT license. The patch adds:

- `--live-message-file` for in-place text refresh.
- `--gamepad-status-file` for a low-latency visual gamepad view.
- A scalable 1280×720 controller layout.
- Highlighted face, shoulder, center, and D-pad buttons.
- Analog stick knobs driven by the live axis values.

The presenter reads the state file every frame and redraws only when values change. It does not restart SDL for each input.

### `launch.sh`

The launcher owns the USB lifecycle. The order is important.

Start:

1. Create `functions/ffs.gamepad`.
2. Mount FunctionFS and start `trimui-gamepad`.
3. Wait until descriptors and strings are ready.
4. Unbind the UDC.
5. Remove the ADB configuration link.
6. Set the temporary Stadia product identity.
7. Link `ffs.gamepad` into configuration `c.1`.
8. Rebind the UDC.
9. Start the visual presenter.

Stop:

1. Unbind the UDC while the FunctionFS process still holds ep0 open.
2. Stop the bridge.
3. Remove the gamepad link and mount.
4. Restore the original product ID and product string.
5. Restore the ADB link.
6. Rebind the UDC.

Closing FunctionFS ep0 before unbinding can crash the vendor USB driver. Do not reorder cleanup.

## Steam-compatible HID protocol

### Why generic HID was not enough

macOS accepted the original generic gamepad descriptor and exposed it through GameController. Steam also enumerated it, but logged:

```text
driver = NONE (DISABLED)
```

Steam did not open an input slot for the unknown `18d1:d002` controller.

### Stadia compatibility mode

Controller mode temporarily uses:

```text
Vendor ID:  0x18d1
Product ID: 0x9400
Product:    Stadia Controller
Report ID:  0x03
```

The HID report descriptor matches the wired Google Stadia controller. The input packet is 11 bytes:

| Byte | Meaning |
|---:|---|
| 0 | Report ID `0x03` |
| 1 | Hat switch, 0–7; 8 is neutral |
| 2 | Back, Guide, Start, right-stick and extra-button bits |
| 3 | A/B/X/Y, L1/R1, left-stick bits |
| 4–5 | Left X/Y, unsigned 1–255, center 128 |
| 6–7 | Right X/Y, unsigned 1–255, center 128 |
| 8–9 | Left/right trigger, 0 or 255 on Smart Pro |
| 10 | Consumer-control bits; currently zero |

Steam selects `SDL_JOYSTICK_HIDAPI_STADIA`, creates a standard gamepad mapping, and reserves input slot 0.

Output report ID `0x05` is declared for protocol compatibility. Rumble writes are acknowledged but are not currently connected to the Smart Pro motor.

## Physical control mapping

| Smart Pro input | Linux event | Internal bit | Stadia/Steam control |
|---|---|---:|---|
| A | `BTN_EAST` | 0 | South / A |
| B | `BTN_SOUTH` | 1 | East / B |
| X | `BTN_WEST` | 2 | West / X |
| Y | `BTN_NORTH` | 3 | North / Y |
| L1 | `BTN_TL` | 4 | Left shoulder |
| R1 | `BTN_TR` | 5 | Right shoulder |
| L2 | `ABS_Z` 0/255 | 6 | Left trigger |
| R2 | `ABS_RZ` 0/255 | 7 | Right trigger |
| SELECT | `BTN_SELECT` | 8 | Back |
| START | `BTN_START` | 9 | Start |
| MENU | `BTN_MODE` | 10 | Guide |
| D-pad | `ABS_HAT0X/Y` | — | Hat 0 |
| Left stick | `ABS_X/Y` | — | Left X/Y |
| Right stick | `ABS_RX/RY` | — | Right X/Y |

The stock input mapping differs from some newer open-source Trimui Inputd mappings, which use `ABS_Z/RZ` for the right stick. The stock Smart Pro uses `ABS_RX/RY`; `ABS_Z/RZ` are the triggers.

## Safety constraints

- Do not expose the live SD block device through USB mass storage.
- Keep FunctionFS ep0 open until after UDC unbind.
- Do not run two controller pak instances.
- Do not install through ADB while controller mode is active.
- Restore the original USB identity and ADB function on every normal exit.
- Treat a 15-second Power hold as recovery only.
