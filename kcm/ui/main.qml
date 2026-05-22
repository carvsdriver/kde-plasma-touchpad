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

        Kirigami.Separator {
            Kirigami.FormData.label: i18n("General")
            Kirigami.FormData.isSection: true
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Theme")
            QQC2.ComboBox {
                Layout.preferredWidth: 200
                model: ListModel {
                    ListElement { text: "System default" }
                    ListElement { text: "Light" }
                    ListElement { text: "Dark" }
                }
                textRole: "text"
                currentIndex: kcm.colorScheme === "light" ? 1 : kcm.colorScheme === "dark" ? 2 : 0
                onActivated: function(idx) {
                    kcm.colorScheme = idx === 1 ? "light" : idx === 2 ? "dark" : "system"
                    kcm.needsSave = true
                }
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Disable screen rotation when active")
            checked: kcm.disableRotation
            onToggled: { kcm.disableRotation = checked; kcm.needsSave = true }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Pointer speed")
            QQC2.Slider {
                id: pointerSlider
                from: 0.1; to: 2.0; stepSize: 0.1
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
                from: 0.1; to: 2.0; stepSize: 0.1
                value: kcm.scrollSpeed
                onMoved: { kcm.scrollSpeed = value; kcm.needsSave = true }
                Layout.preferredWidth: 200
            }
            QQC2.Label { text: scrollSlider.value.toFixed(1) + "×" }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Invert scroll direction")
            checked: kcm.invertScroll
            onToggled: { kcm.invertScroll = checked; kcm.needsSave = true }
        }

        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Buttons and Tap")
            Kirigami.FormData.isSection: true
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Button layout")
            QQC2.ComboBox {
                Layout.preferredWidth: 260
                model: ListModel {
                    ListElement { text: "3 buttons (left, middle, right)" }
                    ListElement { text: "2 buttons (left, right)" }
                    ListElement { text: "1 button (left only)" }
                    ListElement { text: "No buttons" }
                }
                textRole: "text"
                currentIndex: 3 - kcm.buttonCount
                onActivated: function(index) { kcm.buttonCount = 3 - index; kcm.needsSave = true }
            }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Tap to click")
            checked: kcm.tapToClick
            onToggled: { kcm.tapToClick = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger secondary click")
            checked: kcm.twoFingerRightClick
            onToggled: { kcm.twoFingerRightClick = checked; kcm.needsSave = true }
        }

        Kirigami.Separator {
            Kirigami.FormData.label: i18n("Gestures")
            Kirigami.FormData.isSection: true
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Three-finger swipe to switch desktops")
            checked: kcm.threeFingerSwipe
            onToggled: { kcm.threeFingerSwipe = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Pinch to zoom")
            checked: kcm.pinchZoom
            onToggled: { kcm.pinchZoom = checked; kcm.needsSave = true }
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Two-finger rotate (REL_DIAL)")
            checked: kcm.twoFingerRotate
            onToggled: { kcm.twoFingerRotate = checked; kcm.needsSave = true }
        }
    }
}
