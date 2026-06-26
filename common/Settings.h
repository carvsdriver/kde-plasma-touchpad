#pragma once

#include <QString>

// Config file: ~/.config/plasma-touchpadrc
// Group: [Touchpad]

namespace PlasmaTP {

struct Settings {
    int buttonCount = 3;
    QString colorScheme = QStringLiteral("system");
    double pointerSpeed = 0.3;
    double scrollSpeed = 1.0;
    bool disableRotation = true;
    bool threeFingerSwipe = true;
    bool fourFingerShowDesktop = true;
    bool pinchZoom = true;
    bool tapToClick = true;
    bool twoFingerRightClick = true;
    bool twoFingerRotate = true;
    bool invertScroll = false;

    static constexpr const char *configFile() { return "plasma-touchpadrc"; }
    static constexpr const char *groupName() { return "Touchpad"; }
};

} // namespace PlasmaTP
