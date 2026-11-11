# USB Controller for TRIMUI Smart Pro

This NextUI manual pak makes the Smart Pro controls available to a Mac as a standard USB HID gamepad. It uses the existing Linux FunctionFS driver. It does not replace the firmware, modify the kernel, or expose the SD card as USB mass storage.

## Use

1. Use a USB-A-to-USB-C **data** cable. Connect it to the Smart Pro bottom charging/data port. A USB-C-to-C connection does not start device mode reliably on this hardware.
2. In NextUI, open **Tools > USB Controller**.
3. The Mac reconnects to a Steam-compatible `Stadia Controller` HID device. ADB and the WebDAV SD mount pause while controller mode is active because the vendor USB controller cannot bind both functions safely.
4. The interface shows a visual gamepad layout. Pressed buttons and D-pad directions light up, and each stick's on-screen knob follows its movement.
5. To stop controller mode, hold **A + B** together for four seconds, then release both buttons. ADB and the SD mount then return. Waiting for release prevents NextUI from reopening the pak, and the long hold makes accidental exits unlikely.

Controller mode maps the D-pad as a hat switch, both analog sticks as four axes, and A/B/X/Y, L1/R1, L2/R2, SELECT, START, and MENU as buttons. The stock input driver exposes L2/R2 as digital 0/255 axes; the bridge converts them to buttons.

## Recovery

The pak unbinds the HID function before it stops its userspace driver, then restores the normal ADB-only USB configuration. If power is lost or USB does not restore, reboot the Smart Pro. All gadget changes are temporary and a reboot clears them.

## Compatibility

macOS recognizes the interface as a native game controller. Controller mode uses the documented wired Google Stadia identity and HID report format because Steam disables its driver for unknown generic HID devices. Steam selects its enabled Stadia HIDAPI driver and maps the controller to a standard gamepad slot. The normal TRIMUI ADB identity returns on exit.

## Files

- `launch.sh`: creates and removes the temporary composite USB function.
- `trimui-gamepad`: static AArch64 FunctionFS/input bridge.
- `trimui-gamepad.c`: source for the bridge.
- `bin/tg5040/minui-presenter`: NextUI status display with a live visual controller layout.
- `minui-presenter-live.c` and `LICENSE.minui-presenter`: source and license for the patched display.
