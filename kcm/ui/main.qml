import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    KCM.ConfigModule.buttons: KCM.ConfigModule.Default | KCM.ConfigModule.Apply

    Kirigami.FormLayout {
        anchors.fill: parent

        // ── Activation ─────────────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Activation")
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            id: enabledCheck
            Kirigami.FormData.label: i18n("Enable on-screen touchpad")
            checked: kcm.activateOnMask !== 0
            onToggled: { kcm.activateOnMask = checked ? 0x3 : 0; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            id: topHalfCheck
            Kirigami.FormData.label: i18n("Activate when keyboard is on top half")
            enabled: enabledCheck.checked
            checked: (kcm.activateOnMask & 0x1) !== 0
            onToggled: {
                kcm.activateOnMask = checked ? (kcm.activateOnMask | 0x1)
                                             : (kcm.activateOnMask & ~0x1)
                kcm.needsSave = true
            }
        }

        QQC2.CheckBox {
            id: bottomHalfCheck
            Kirigami.FormData.label: i18n("Activate when keyboard is on bottom half")
            enabled: enabledCheck.checked
            checked: (kcm.activateOnMask & 0x2) !== 0
            onToggled: {
                kcm.activateOnMask = checked ? (kcm.activateOnMask | 0x2)
                                             : (kcm.activateOnMask & ~0x2)
                kcm.needsSave = true
            }
        }

        // ── Pointer ────────────────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Pointer")
            Kirigami.FormData.isSection: true
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Pointer speed")
            QQC2.Slider {
                id: pointerSlider
                from: 0.1; to: 5.0; stepSize: 0.1
                value: kcm.pointerSpeed
                onMoved: { kcm.pointerSpeed = value; kcm.needsSave = true }
                Layout.preferredWidth: 200
            }
            QQC2.Label { text: pointerSlider.value.toFixed(1) + "×" }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Scroll speed")
            QQC2.Slider {
                id: scrollSlider
                from: 0.1; to: 5.0; stepSize: 0.1
                value: kcm.scrollSpeed
                onMoved: { kcm.scrollSpeed = value; kcm.needsSave = true }
                Layout.preferredWidth: 200
            }
            QQC2.Label { text: scrollSlider.value.toFixed(1) + "×" }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Tap to click")
            checked: kcm.tapToClick
            onToggled: { kcm.tapToClick = checked; kcm.needsSave = true }
        }

        // ── Buttons ────────────────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Buttons")
            Kirigami.FormData.isSection: true
        }

        QQC2.ComboBox {
            id: buttonCountCombo
            Kirigami.FormData.label: i18n("Button layout")
            model: [
                i18n("None — tap to click only (MacBook style)"),
                i18n("1 button  (left click)"),
                i18n("2 buttons  (left / right)"),
                i18n("3 buttons  (left / middle / right)"),
            ]
            currentIndex: kcm.buttonCount
            onActivated: { kcm.buttonCount = currentIndex; kcm.needsSave = true }
        }

        // ── Appearance ─────────────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Appearance")
            Kirigami.FormData.isSection: true
        }

        QQC2.ComboBox {
            id: colorSchemeCombo
            Kirigami.FormData.label: i18n("Color scheme")

            // Keep in sync with the "system"/"light"/"dark" string values
            property var schemeIds: ["system", "light", "dark"]
            model: [
                i18n("Follow system theme"),
                i18n("Light"),
                i18n("Dark"),
            ]
            currentIndex: {
                const idx = schemeIds.indexOf(kcm.colorScheme)
                return idx >= 0 ? idx : 0
            }
            onActivated: {
                kcm.colorScheme = schemeIds[currentIndex]
                kcm.needsSave = true
            }
        }

        // ── Gestures ───────────────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Gestures")
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger tap → right click")
            checked: kcm.gestureRightClick
            onToggled: { kcm.gestureRightClick = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger double-tap → smart zoom")
            checked: kcm.gestureSmartZoom
            onToggled: { kcm.gestureSmartZoom = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger scroll")
            checked: kcm.gestureScroll
            onToggled: { kcm.gestureScroll = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Pinch to zoom")
            checked: kcm.gesturePinchZoom
            onToggled: { kcm.gesturePinchZoom = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger rotate")
            checked: kcm.gestureRotate
            onToggled: { kcm.gestureRotate = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger swipe → previous / next page")
            checked: kcm.gestureSwipePages
            onToggled: { kcm.gestureSwipePages = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Four-finger swipe → switch desktops")
            checked: kcm.gestureSwipeDesktops
            onToggled: { kcm.gestureSwipeDesktops = checked; kcm.needsSave = true }
        }

        // ── Screen management ──────────────────────────────────────────────
        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Screen Management")
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Move panels to top screen when active")
            checked: kcm.movePanels
            onToggled: { kcm.movePanels = checked; kcm.needsSave = true }
        }
    }
}
