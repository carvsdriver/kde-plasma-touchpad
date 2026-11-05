import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import "feed.js" as Feed

PlasmoidItem {
    id: root

    // Every headline from every feed, newest first; each has a .feed (its
    // feed's address).
    property var allItems: []
    // The headlines shown: from the selected feed, or all of them.
    readonly property var items: {
        const feed = Plasmoid.configuration.selectedFeed;
        const list = feed ? allItems.filter(i => i.feed === feed) : allItems;
        return list.slice(0, Plasmoid.configuration.maxItems);
    }
    // Feed address -> the feed's own title, once loaded.
    property var feedTitles: ({})
    readonly property var feeds: Plasmoid.configuration.feeds.map(f => f.trim()).filter(f => f)
    property bool loading: false
    property string error: ""
    property double now: Date.now()
    // The headline shown in the widget, or null for the list.
    property var viewing: null

    preferredRepresentation: fullRepresentation
    Plasmoid.icon: "news-subscribe"

    // Open a headline: in the widget, or in the default browser if so set.
    function open(item) {
        if (Plasmoid.configuration.openIn === "browser")
            Qt.openUrlExternally(item.link);
        else
            viewing = item;
    }

    // A feed's title, or its site's address until it has loaded.
    function feedTitle(url) {
        if (feedTitles[url])
            return feedTitles[url];
        const host = url.match(/^https?:\/\/([^\/]+)/);
        return host ? host[1] : url;
    }

    function selectFeed(url) {
        Plasmoid.configuration.selectedFeed = url;
    }

    function refresh() {
        // A removed feed can't stay selected.
        if (Plasmoid.configuration.selectedFeed && feeds.indexOf(Plasmoid.configuration.selectedFeed) < 0)
            selectFeed("");
        if (!feeds.length) {
            allItems = [];
            error = i18n("No feeds set up yet.");
            return;
        }
        loading = true;
        let collected = [];
        let left = feeds.length;
        let failed = 0;
        let titles = {};
        feeds.forEach(url => {
            const xhr = new XMLHttpRequest();
            xhr.onreadystatechange = () => {
                if (xhr.readyState !== XMLHttpRequest.DONE)
                    return;
                const parsed = xhr.status === 200 ? Feed.parse(xhr.responseXML) : [];
                if (!parsed.length) {
                    failed++;
                    console.warn("Dashboard News: no headlines from", url, "HTTP", xhr.status);
                }
                parsed.forEach(i => i.feed = url);
                if (parsed.length && parsed[0].source)
                    titles[url] = parsed[0].source;
                collected = collected.concat(parsed);
                if (--left === 0) {
                    feedTitles = titles;
                    finish(collected, failed);
                }
            };
            xhr.open("GET", url);
            xhr.send();
        });
    }

    function finish(collected, failed) {
        const seen = {};
        const unique = collected.filter(i => {
            if (seen[i.link])
                return false;
            seen[i.link] = true;
            return true;
        });
        // Newest first; undated headlines keep their feed order, after the
        // dated ones.
        unique.forEach((item, index) => item.index = index);
        unique.sort((a, b) => (b.time - a.time) || (a.index - b.index));
        allItems = unique;
        now = Date.now();
        loading = false;
        error = allItems.length ? "" : (failed ? i18n("Could not load the news feeds.") : i18n("No headlines."));
    }

    Component.onCompleted: refresh()

    Connections {
        target: Plasmoid.configuration
        function onFeedsChanged() { root.refresh(); }
    }

    Timer {
        interval: Plasmoid.configuration.refreshMinutes * 60000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    // Keeps the "5 min" ages current between refreshes.
    Timer {
        interval: 60000
        running: true
        repeat: true
        onTriggered: root.now = Date.now()
    }

    fullRepresentation: Item {
        Layout.preferredWidth: Kirigami.Units.gridUnit * 20
        Layout.preferredHeight: Kirigami.Units.gridUnit * 18

        ColumnLayout {
            anchors.fill: parent
            visible: root.viewing === null
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents.ToolButton {
                    id: menuButton
                    icon.name: "application-menu"
                    text: i18n("Choose Feed")
                    display: PlasmaComponents.AbstractButton.IconOnly
                    checkable: true
                    checked: feedMenu.visible
                    onClicked: feedMenu.popup(menuButton, 0, menuButton.height)
                    PlasmaComponents.ToolTip.text: text
                    PlasmaComponents.ToolTip.visible: hovered && !feedMenu.visible

                    PlasmaComponents.Menu {
                        id: feedMenu

                        PlasmaComponents.MenuItem {
                            text: i18n("All Feeds")
                            checkable: true
                            checked: !Plasmoid.configuration.selectedFeed
                            onTriggered: root.selectFeed("")
                        }
                        PlasmaComponents.MenuSeparator {}
                        // One item per feed, inserted after the separator.
                        Instantiator {
                            model: root.feeds
                            delegate: PlasmaComponents.MenuItem {
                                required property string modelData
                                text: root.feedTitle(modelData)
                                checkable: true
                                checked: Plasmoid.configuration.selectedFeed === modelData
                                onTriggered: root.selectFeed(modelData)
                            }
                            onObjectAdded: (index, object) => feedMenu.insertItem(index + 2, object)
                            onObjectRemoved: (index, object) => feedMenu.removeItem(object)
                        }
                        PlasmaComponents.MenuSeparator {}
                        PlasmaComponents.MenuItem {
                            icon.name: "list-add"
                            text: i18n("Add Feeds…")
                            onTriggered: Plasmoid.internalAction("configure").trigger()
                        }
                    }
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: Plasmoid.configuration.selectedFeed
                          ? root.feedTitle(Plasmoid.configuration.selectedFeed)
                          : i18n("All Feeds")
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                PlasmaComponents.BusyIndicator {
                    visible: root.loading
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                }
                PlasmaComponents.ToolButton {
                    icon.name: "view-refresh"
                    enabled: !root.loading
                    onClicked: root.refresh()
                    PlasmaComponents.ToolTip.text: i18n("Refresh")
                    PlasmaComponents.ToolTip.visible: hovered
                }
            }

            PlasmaComponents.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    clip: true
                    model: root.items
                    spacing: 0

                    delegate: PlasmaComponents.ItemDelegate {
                        required property var modelData
                        width: ListView.view.width
                        topPadding: Kirigami.Units.largeSpacing
                        bottomPadding: Kirigami.Units.largeSpacing
                        onClicked: root.open(modelData)

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing / 2
                            PlasmaComponents.Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                            PlasmaComponents.Label {
                                Layout.fillWidth: true
                                text: [modelData.source, Feed.age(modelData.time, root.now)]
                                          .filter(s => s).join(" · ")
                                visible: text.length > 0
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                        }
                    }

                    PlasmaExtras.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 2
                        visible: !root.loading && root.items.length === 0
                        iconName: "news-subscribe"
                        text: root.error
                        helpfulAction: Kirigami.Action {
                            icon.name: "configure"
                            text: i18n("Configure…")
                            onTriggered: Plasmoid.internalAction("configure").trigger()
                        }
                    }
                }
            }

            PlasmaComponents.ToolButton {
                Layout.alignment: Qt.AlignHCenter
                icon.name: "list-add"
                text: i18n("Add Feeds…")
                onClicked: Plasmoid.internalAction("configure").trigger()
            }
        }

        // The item being viewed in the widget.  Unloading the viewer stops
        // anything playing.
        Loader {
            anchors.fill: parent
            active: root.viewing !== null
            sourceComponent: Viewer {
                title: root.viewing?.title ?? ""
                pageUrl: root.viewing?.link ?? ""
                onBack: root.viewing = null
            }
        }
    }
}
