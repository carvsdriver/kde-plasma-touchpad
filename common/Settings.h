#pragma once

#include <QString>

// Config file: ~/.config/plasma-touchpadrc
// Group: [Touchpad]

namespace PlasmaTP {

struct Settings {
    // Pointer speed multiplier (0.1 – 5.0, default 0.35)
    double pointerSpeed = 0.35;
    // Scroll speed multiplier (0.1 – 5.0, default 1.0)
    double scrollSpeed = 1.0;
    // Enable tap-to-click in the trackpad zone
    bool tapToClick = true;
    // Move panels off the bottom screen when the touchpad activates
    bool movePanels = true;
    // BKBD positions that activate the touchpad (bitmask: bit0=top-half, bit1=bottom-half)
    int activateOnMask = 0x3;   // both positions by default
    // Number of physical buttons shown (0 = MacBook mode, tap-to-click only)
    int buttonCount = 3;
    // Overlay colour scheme: "system" | "light" | "dark"
    QString colorScheme = QStringLiteral("system");

    // Gesture flags (all enabled by default)
    bool gestureRightClick = true;
    bool gestureSmartZoom = true;
    bool gestureScroll = true;
    bool gesturePinchZoom = true;
    bool gestureRotate = true;
    bool gestureSwipePages = true;
    bool gestureSwipeDesktops = true;

    static constexpr const char *configFile() { return "plasma-touchpadrc"; }
    static constexpr const char *groupName() { return "Touchpad"; }
};

} // namespace PlasmaTP
