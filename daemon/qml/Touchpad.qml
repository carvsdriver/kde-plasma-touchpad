import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Root item fills the overlay window (full width, half of eDP-2 height).
// Layout: [palm rest | trackpad + buttons | palm rest]  (each 1/3 width)
//
Item {
    id: root
    anchors.fill: parent

    // Set by TouchpadOverlay via rootObject()->setProperty() — fully reactive.
    property int buttonCount: 3
    property string colorScheme: "system"
    property int pressedButtonIndex: -1

    // ── Colour scheme ───────────────────────────────────────────────────────
    // Kirigami.Theme is bound to KDE's color scheme system and updates live
    // when the user switches themes — unlike SystemPalette which is static.

    readonly property color bgColor:
        colorScheme === "dark" ? "#2d2d2d" :
        colorScheme === "light" ? "#f0f0f0" :
        Kirigami.Theme.backgroundColor

    readonly property color palmColor:
        colorScheme === "dark" ? "#252525" :
        colorScheme === "light" ? "#e8e8e8" :
        Qt.darker(Kirigami.Theme.backgroundColor, 1.06)

    readonly property color borderColor:
        colorScheme === "dark" ? "#1a1a1a" :
        colorScheme === "light" ? "#d0d0d0" :
        Qt.darker(Kirigami.Theme.backgroundColor, 1.18)

    readonly property color dimTextColor:
        colorScheme === "dark" ? "#555555" :
        colorScheme === "light" ? "#c0c0c0" :
        Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                Kirigami.Theme.textColor.b, 0.22)

    readonly property color btnBg:
        colorScheme === "dark" ? "#3c3c3c" :
        colorScheme === "light" ? "#e0e0e0" :
        Kirigami.Theme.alternateBackgroundColor

    readonly property color btnText:
        colorScheme === "dark" ? "#dddddd" :
        colorScheme === "light" ? "#222222" :
        Kirigami.Theme.textColor

    readonly property color btnHighlight:
        colorScheme === "dark" ? "#5a8fc2" :
        colorScheme === "light" ? "#4a90d9" :
        Kirigami.Theme.highlightColor

    readonly property color btnHighlightText:
        colorScheme === "dark" ? "#ffffff" :
        colorScheme === "light" ? "#ffffff" :
        Kirigami.Theme.highlightedTextColor

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

                        readonly property bool isPressed: index === root.pressedButtonIndex

                        color: isPressed ? root.btnHighlight : root.btnBg
                        border.color: root.borderColor
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: parent.isPressed ? root.btnHighlightText : root.btnText
                            font.pixelSize: 14
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
