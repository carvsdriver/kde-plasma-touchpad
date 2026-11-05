import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: page

    property var cfg_feeds: []
    property alias cfg_maxItems: maxItems.value
    property alias cfg_refreshMinutes: refreshMinutes.value
    property string cfg_openIn: "widget"

    function canAdd(text) {
        const url = text.trim();
        return /^https?:\/\/\S+$/.test(url) && cfg_feeds.indexOf(url) < 0;
    }

    function addFeed() {
        if (!canAdd(newFeed.text))
            return;
        cfg_feeds = cfg_feeds.concat([newFeed.text.trim()]);
        newFeed.text = "";
    }

    Kirigami.FormLayout {
        ColumnLayout {
            Kirigami.FormData.label: i18n("Feeds:")
            Layout.fillWidth: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 20
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: cfg_feeds
                delegate: RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true

                    QQC2.Label {
                        Layout.fillWidth: true
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                        text: modelData
                        elide: Text.ElideMiddle
                        textFormat: Text.PlainText
                    }
                    QQC2.ToolButton {
                        icon.name: "list-remove"
                        text: i18n("Remove")
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: cfg_feeds = cfg_feeds.filter((f, i) => i !== index)
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                    }
                }
            }

            QQC2.Label {
                visible: cfg_feeds.length === 0
                text: i18n("No feeds yet.")
                opacity: 0.7
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.TextField {
                    id: newFeed
                    Layout.fillWidth: true
                    placeholderText: i18n("Feed address, e.g. https://example.com/rss.xml")
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: page.addFeed()
                }
                QQC2.Button {
                    icon.name: "list-add"
                    text: i18n("Add")
                    enabled: page.canAdd(newFeed.text)
                    onClicked: page.addFeed()
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                text: i18n("Any RSS or Atom feed. Headlines from all feeds are shown together, newest first.")
            }
        }

        QQC2.SpinBox {
            id: maxItems
            Kirigami.FormData.label: i18n("Headlines to show:")
            from: 5
            to: 200
        }

        QQC2.SpinBox {
            id: refreshMinutes
            Kirigami.FormData.label: i18n("Refresh every:")
            from: 1
            to: 1440
            textFromValue: (value) => i18np("%1 minute", "%1 minutes", value)
            valueFromText: (text) => parseInt(text)
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18n("When tapped:")
            text: i18n("Open in the widget")
            checked: cfg_openIn !== "browser"
            onToggled: if (checked) cfg_openIn = "widget"
        }
        QQC2.RadioButton {
            text: i18n("Open in the browser")
            checked: cfg_openIn === "browser"
            onToggled: if (checked) cfg_openIn = "browser"
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            text: i18n("Sites that need you to sign in work best in the browser, where its password manager and extensions are available.")
        }
    }
}
