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
- **Two-finger rotate** — REL_DIAL events (understood by audio knobs, drawing apps, etc.)
- **Three-finger horizontal swipe** — Ctrl+Alt+Left/Right (KDE virtual desktop switching)
- **Screen rotation suppression** — pauses `rotate-screens.sh` while the touchpad is active
- **Window management** — moves windows from the bottom screen to the top screen while the touchpad is active, and restores them on deactivation
- **Panel management** — moves taskbar panels to the top screen while the touchpad is active
- **Virtual keyboard suppression** — disables the KWin OSK while the touchpad overlay is visible
- **Auto-activation** — detects keyboard dock position via the `lenovo-yb9-kbdock` driver
- **Live colour scheme** — overlay tracks KDE colour scheme changes in real time; can also be forced light or dark
- **System Settings KCM** — all options configurable under *Input Devices → On-Screen Touchpad*

## Requirements

### Kernel support

All required kernel changes are merged in mainline. **Linux 7.3 is the first release with full support**, and on 7.3 or later no patches or out-of-tree modules are needed. Fedora's kernel config already enables the dock driver (`CONFIG_LENOVO_YB9_KBDOCK=m`), so the stock Fedora kernel works once it moves to 7.3.

| # | Module | Purpose | Upstream commit | First release |
|---|--------|---------|-----------------|---------------|
| 0001 | `hid-multitouch` | Fix touchscreen HID descriptor for the INGENIC controller | [f1bd44b9b62c](https://git.kernel.org/torvalds/c/f1bd44b9b62c) | 7.2 |
| 0002 | `hid-multitouch` | Clear stale MT slots when contact count drops to zero | [e8e60b6439ee](https://git.kernel.org/torvalds/c/e8e60b6439ee) | 7.3 |
| 0003 | `hid-multitouch` | Honor ContactCount to suppress ghost contacts | [ef257b8be977](https://git.kernel.org/torvalds/c/ef257b8be977) | 7.2 |
| 0004 | `cdc-acm` | Add INGENIC 17EF:6161 quirks (`VENDOR_CLASS_DATA_IFACE` + `ALWAYS_POLL_CTRL`) | [f58752ebcb35](https://git.kernel.org/torvalds/c/f58752ebcb35) | 7.1 |
| 0005 | `cdc-acm` | Keep bulk-IN polling when `ALWAYS_POLL_CTRL` is set (prevents ~55 s watchdog reset) | [e5ab27ddd74e](https://git.kernel.org/torvalds/c/e5ab27ddd74e) | 7.2 |
| 0006 | `lenovo-yb9-kbdock` | New driver: detects keyboard dock attach/detach and exposes dock position | [c9b5c8ff065d](https://git.kernel.org/torvalds/c/c9b5c8ff065d) | 7.3 |
| 0007 | `lenovo-ymc` | Skip probe on this model so the dock driver is the only tablet-mode switch | [bdd0f4e3333f](https://git.kernel.org/torvalds/c/bdd0f4e3333f) | 7.3 |

On kernels older than 7.3, patches 0002, 0006 and 0007 must be built locally (0001, 0003 and 0005 as well on 7.1). Without 0006 the touchpad cannot auto-activate when the keyboard is docked. See the patch documentation and `rebuild-patches.sh` here: https://bitbucket.org/carvsdriver/lenovoyoga9ibook/src/main/

## Install (Fedora)

Prebuilt packages for current Fedora releases are available from COPR. Requires Linux 7.3 or later, or the patched modules described above.

```bash
sudo dnf copr enable carvsdriver/yoga-book-9
sudo dnf install plasma-touchpad
```

The daemon runs as a systemd user service and is enabled automatically; log out and back in after installing.

## Building from source

### Build dependencies (Fedora)

```
sudo dnf install cmake extra-cmake-modules \
    qt6-qtbase-devel qt6-qtdeclarative-devel \
    kf6-kcmutils-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
    kf6-ki18n-devel kf6-kwindowsystem-devel \
    layer-shell-qt-devel
```

### Build

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
| `twoFingerRotate` | bool | true | Two-finger rotate → REL_DIAL |
| `pinchZoom` | bool | true | Pinch/spread → Ctrl+Wheel |
| `threeFingerSwipe` | bool | true | Three-finger horizontal swipe → virtual desktop switch |
| `disableRotation` | bool | true | Pause screen rotation script while touchpad is active |
| `colorScheme` | string | system | Overlay theme: `system`, `light`, `dark` |

## License

Author: Dave Carey <carvsdriver@gmail.com>
GPL-2.0-or-later
