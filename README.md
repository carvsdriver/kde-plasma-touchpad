# plasma-touchpad

On-screen touchpad daemon and KCM for the **Lenovo Yoga Book 9 14IAH10**.

The Yoga Book 9 has a second screen on the bottom half. When the keyboard dock is attached, the lower portion of that screen functions as a touchpad. This project intercepts raw multitouch events from the INGENIC touchscreen controller, translates them into standard pointer and gesture input, and exposes a System Settings panel for configuration.

## Features

- **Pointer movement** — single-finger cursor control with configurable speed
- **Tap-to-click** — single-finger tap emits left click
- **Physical button strip** — configurable 0 / 1 / 2 / 3-button layout along the bottom edge
- **Two-finger scroll** — vertical scroll with configurable speed
- **Two-finger tap** — right click
- **Two-finger pinch/spread** — Ctrl+Wheel (application zoom)
- **Three-finger horizontal swipe** — Ctrl+Alt+Left/Right (KDE virtual desktop switching)
- **Screen rotation suppression** — pauses `rotate-screens.sh` while the touchpad is active
- **Window management** — moves windows from the bottom screen to the top screen while the touchpad is active, and restores them on deactivation
- **Panel management** — moves taskbar panels to the top screen while the touchpad is active
- **Virtual keyboard suppression** — disables the KWin OSK while the touchpad overlay is visible
- **Auto-activation** — detects keyboard dock position via the `yb9-kbdock` driver
- **Live colour scheme** — overlay tracks KDE colour scheme changes in real time; can also be forced light or dark
- **System Settings KCM** — all options configurable under *Input Devices → On-Screen Touchpad*

## Requirements

### Kernel patches

The hardware requires three out-of-tree kernel patches and a new driver.

| # | Module | Purpose |
|---|--------|---------|
| 0001 | `hid-multitouch` | Fix touchscreen HID descriptor for the INGENIC controller |
| 0002 | `cdc-acm` | Add INGENIC 17EF:6161 quirk (`VENDOR_CLASS_DATA_IFACE` + `ALWAYS_POLL_CTRL`) |
| 0003 | `cdc-acm` | Keep bulk-IN endpoint polling when `ALWAYS_POLL_CTRL` is set (prevents ~55 s watchdog reset) |
| 0004 | `yb9-kbdock` | New driver — detects keyboard dock attach/detach and exposes dock position |

Before proceeding, read the documentation here: https://bitbucket.org/carvsdriver/lenovoyoga9ibook/src/main/

### Build dependencies (Fedora)

```
sudo dnf install cmake extra-cmake-modules \
    qt6-qtbase-devel qt6-qtdeclarative-devel \
    kf6-kcmutils-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
    kf6-ki18n-devel kf6-kwindowsystem-devel \
    layer-shell-qt-devel
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

## Architecture Note

The keyboard events are on a **separate** uinput device deliberately. Advertising keyboard keycodes on the pointer device causes libinput to classify it as a keyboard, which triggers KDE's *hide cursor while typing* feature on every gesture keystroke.

## Configuration

Settings are stored in `~/.config/plasma-touchpadrc` under `[Touchpad]`.

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `pointerSpeed` | double | 0.3 | Cursor speed multiplier (0.1 – 2.0) |
| `scrollSpeed` | double | 1.0 | Scroll speed multiplier (0.1 – 2.0) |
| `buttonCount` | int | 3 | Physical button strip: 0 = none, 1 / 2 / 3 = buttons |
| `tapToClick` | bool | true | Single-finger tap → left click |
| `twoFingerRightClick` | bool | true | Two-finger tap → right click |
| `pinchZoom` | bool | true | Pinch/spread → Ctrl+Wheel |
| `threeFingerSwipe` | bool | true | Three-finger horizontal swipe → virtual desktop switch |
| `disableRotation` | bool | true | Pause screen rotation script while touchpad is active |
| `colorScheme` | string | system | Overlay theme: `system`, `light`, `dark` |

## License

Author: Dave Carey <carvsdriver@gmail.com>
GPL-2.0-or-later
