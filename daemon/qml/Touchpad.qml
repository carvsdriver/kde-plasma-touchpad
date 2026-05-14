import QtQuick
import QtQuick.Layouts

// Root item fills the overlay window (full width, half of eDP-2 height).
// Layout: [palm rest | trackpad + buttons | palm rest]  (each 1/3 width)
//
// Context properties injected by TouchpadOverlay:
//   int    buttonCount  — 0 (MacBook) / 1 / 2 / 3
//   string colorScheme  — "system" | "light" | "dark"

Item {
    id: root
    anchors.fill: parent

    // ── Colour scheme ───────────────────────────────────────────────────────
    SystemPalette { id: sysPalette; colorGroup: SystemPalette.Active }

    readonly property color bgColor:
        colorScheme === "dark" ? "#2d2d2d" :
        colorScheme === "light" ? "#f0f0f0" :
        sysPalette.window

    readonly property color palmColor:
        colorScheme === "dark" ? "#252525" :
        colorScheme === "light" ? "#e8e8e8" :
        Qt.darker(sysPalette.window, 1.06)

    readonly property color borderColor:
        colorScheme === "dark" ? "#1a1a1a" :
        colorScheme === "light" ? "#d0d0d0" :
        Qt.darker(sysPalette.window, 1.18)

    readonly property color dimTextColor:
        colorScheme === "dark" ? "#555555" :
        colorScheme === "light" ? "#c0c0c0" :
        Qt.rgba(sysPalette.windowText.r, sysPalette.windowText.g,
                sysPalette.windowText.b, 0.22)

    readonly property color btnBg:
        colorScheme === "dark" ? "#3c3c3c" :
        colorScheme === "light" ? "#e0e0e0" :
        sysPalette.button

    readonly property color btnText:
        colorScheme === "dark" ? "#dddddd" :
        colorScheme === "light" ? "#222222" :
        sysPalette.buttonText

    readonly property color btnHighlight:
        colorScheme === "dark" ? "#5a8fc2" :
        colorScheme === "light" ? "#4a90d9" :
        sysPalette.highlight

    readonly property color btnHighlightText:
        colorScheme === "dark" ? "#ffffff" :
        colorScheme === "light" ? "#ffffff" :
        sysPalette.highlightedText

    // ── Button model ────────────────────────────────────────────────────────
    readonly property var buttonModel:
        buttonCount >= 3 ? [{ label: "◀" }, { label: "●" }, { label: "▶" }] :
        buttonCount === 2 ? [{ label: "◀" }, { label: "▶" }] :
        buttonCount === 1 ? [{ label: "●" }] :
        []

    // ── Background ──────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: root.bgColor
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left palm rest ──────────────────────────────────────────────────
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width / 3
            color: root.palmColor
        }

        // ── Centre: trackpad surface + button strip ─────────────────────────
        Rectangle {
            id: trackpadArea
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width / 3
            color: root.bgColor
            border.color: root.borderColor
            border.width: 1
            radius: 4

            // Button strip — hidden in MacBook mode (buttonCount === 0)
            RowLayout {
                id: buttonStrip
                visible: buttonCount > 0
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: parent.height * 0.12
                spacing: 0

                Repeater {
                    model: root.buttonModel

                    delegate: Rectangle {
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        color: mouseArea.pressed ? root.btnHighlight : root.btnBg
                        border.color: root.borderColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: mouseArea.pressed ? root.btnHighlightText : root.btnText
                            font.pixelSize: 14
                        }

                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            // Visual feedback only — clicks come from TouchInputHandler via uinput
                        }
                    }
                }
            }
        }

        // ── Right palm rest ─────────────────────────────────────────────────
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width / 3
            color: root.palmColor
        }
    }
}
