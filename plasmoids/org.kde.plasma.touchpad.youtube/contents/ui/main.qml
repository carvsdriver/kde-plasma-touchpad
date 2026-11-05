import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import org.kde.plasma.workspace.dbus as DBus
import "feed.js" as Feed

PlasmoidItem {
    id: root

    property var videos: []
    property bool loading: false
    property string error: ""
    property double now: Date.now()
    // The video playing in the widget, or null for the grid.
    property var viewing: null

    // The touchpad daemon's YouTube account (see YouTubeAccount.h):
    // "signed-in", "signed-out", "signing-in", "not-configured",
    // "unavailable" (daemon not running), or "" before the first answer.
    property string accountStatus: ""
    // Retries while a fresh sign-in's subscription list is still on its way.
    property int subscriptionRetries: 0

    preferredRepresentation: fullRepresentation
    Plasmoid.icon: "video-television"

    // Open a video: play it in the widget, or in the default browser if so
    // set.
    function open(video) {
        if (Plasmoid.configuration.openIn === "browser")
            Qt.openUrlExternally(video.link);
        else
            viewing = video;
    }

    // A page holding YouTube's embedded player for video `id`.  It reports
    // back through the viewer's console messages: "ready" once the player
    // loads, and "error:<code>" if it cannot play the video, or
    // "error:timeout" if the player never loads.
    function playerPage(id) {
        return `<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width">
<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}#player{position:fixed;inset:0;width:100%;height:100%}</style>
</head><body><div id="player"></div><script>
var ready = false;
function report(m) { console.log("plasma-touchpad:" + m); }
function onYouTubeIframeAPIReady() {
  new YT.Player("player", { width: "100%", height: "100%", videoId: ${JSON.stringify(id)},
    playerVars: { autoplay: 1, playsinline: 1, rel: 0 },
    events: { onReady: function () { ready = true; report("ready"); },
              onError: function (e) { report("error:" + e.data); } } });
}
setTimeout(function () { if (!ready) report("error:timeout"); }, 15000);
</script><script src="https://www.youtube.com/iframe_api"></script></body></html>`;
    }

    function get(url, done) {
        const xhr = new XMLHttpRequest();
        xhr.onreadystatechange = () => {
            if (xhr.readyState === XMLHttpRequest.DONE)
                done(xhr);
        };
        xhr.open("GET", url);
        xhr.send();
    }

    function videoId(link) {
        const m = link.match(/(?:[?&]v=|\/shorts\/)([\w-]{11})/);
        return m ? m[1] : "";
    }

    // Call the touchpad daemon's YouTube account (see YouTubeAccount.h).
    function accountCall(member, done) {
        DBus.SessionBus.asyncCall({
            service: "org.kde.plasma.touchpad",
            path: "/YouTube",
            iface: "org.kde.plasma.touchpad.YouTube",
            member: member,
            arguments: []
        }, reply => {
            let value = reply.value;
            if (value && value.value !== undefined)
                value = value.value;
            if (done)
                done(String(value ?? ""));
        }, () => accountStatus = "unavailable");
    }

    function checkAccount() {
        accountCall("status", status => {
            const was = accountStatus;
            accountStatus = status;
            if (status === "signed-in" && was !== "signed-in") {
                subscriptionRetries = 0;
                refresh();
            } else if (status !== "signed-in") {
                videos = [];
            }
        });
    }

    function refresh() {
        if (accountStatus !== "signed-in") {
            checkAccount();
            return;
        }
        loading = true;
        accountCall("subscriptions", json => {
            let channels = [];
            try { channels = JSON.parse(json).map(c => c.id); } catch (e) {}
            if (!channels.length) {
                videos = [];
                loading = false;
                // Just signed in: the daemon may still be fetching the list.
                if (subscriptionRetries < 4) {
                    subscriptionRetries++;
                    error = i18n("Loading your subscriptions…");
                    retry.start();
                } else {
                    error = i18n("Your YouTube account has no subscriptions.");
                }
                return;
            }
            subscriptionRetries = 0;
            load(channels);
        });
    }

    // Fetch the feeds of `channels` (channel IDs), a few at a time.
    function load(channels) {
        let collected = [];
        let failed = 0;
        let next = 0;
        let left = channels.length;
        const startNext = () => {
            if (next >= channels.length)
                return;
            get("https://www.youtube.com/feeds/videos.xml?channel_id=" + channels[next++], xhr => {
                const list = xhr.status === 200 ? Feed.parse(xhr.responseXML) : [];
                if (!list.length)
                    failed++;
                collected = collected.concat(list);
                if (--left === 0)
                    finish(collected, failed);
                else
                    startNext();
            });
        };
        for (let i = 0; i < 6; i++)
            startNext();
    }

    function finish(collected, failed) {
        const seen = {};
        let list = collected.filter(v => {
            v.id = videoId(v.link);
            if (!v.id || seen[v.id])
                return false;
            if (Plasmoid.configuration.hideShorts && v.link.indexOf("/shorts/") >= 0)
                return false;
            seen[v.id] = true;
            return true;
        });
        list.sort((a, b) => b.time - a.time);
        videos = list.slice(0, Plasmoid.configuration.maxItems);
        now = Date.now();
        loading = false;
        error = videos.length ? "" : (failed ? i18n("Could not load the channel feeds.") : i18n("No videos."));
    }

    Component.onCompleted: checkAccount()

    Connections {
        target: Plasmoid.configuration
        function onHideShortsChanged() { root.refresh(); }
        function onMaxItemsChanged() { root.refresh(); }
    }

    Timer {
        interval: Plasmoid.configuration.refreshMinutes * 60000
        running: true
        repeat: true
        onTriggered: root.refresh()
    }

    // Notices signing in (which finishes in the browser) and signing out.
    Timer {
        interval: root.accountStatus === "signed-in" ? 60000 : 3000
        running: true
        repeat: true
        onTriggered: root.checkAccount()
    }

    Timer {
        id: retry
        interval: 15000
        onTriggered: root.refresh()
    }

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

                Kirigami.Icon {
                    source: "video-television"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: Plasmoid.configuration.title
                    elide: Text.ElideRight
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

                GridView {
                    id: grid
                    clip: true
                    model: root.videos

                    readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 11)))
                    cellWidth: Math.floor(width / columns)
                    cellHeight: Math.round((cellWidth - Kirigami.Units.smallSpacing * 2) * 9 / 16)
                                + Kirigami.Units.gridUnit * 3.5

                    delegate: PlasmaComponents.ItemDelegate {
                        required property var modelData
                        width: grid.cellWidth
                        height: grid.cellHeight
                        padding: Kirigami.Units.smallSpacing
                        onClicked: root.open(modelData)

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing / 2

                            Image {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.round(width * 9 / 16)
                                source: "https://i.ytimg.com/vi/" + modelData.id + "/mqdefault.jpg"
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                Rectangle {
                                    anchors.fill: parent
                                    visible: parent.status !== Image.Ready
                                    color: Kirigami.Theme.alternateBackgroundColor
                                }
                            }
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
                                text: [modelData.author || modelData.source, Feed.age(modelData.time, root.now)]
                                          .filter(s => s).join(" · ")
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }

                    PlasmaExtras.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 2
                        visible: !root.loading && root.videos.length === 0 && root.accountStatus !== ""
                        iconName: root.accountStatus === "signed-in" ? "video-television" : "user-identity"
                        text: {
                            switch (root.accountStatus) {
                            case "signed-in":
                                return root.error;
                            case "signed-out":
                                return i18n("Sign in to YouTube to see the latest videos from your subscriptions.");
                            case "signing-in":
                                return i18n("Finish signing in in your browser…");
                            case "not-configured":
                                return i18n("Sign in to YouTube to see the latest videos from your subscriptions.");
                            case "unavailable":
                                return i18n("The touchpad service isn't running.");
                            }
                            return "";
                        }
                        explanation: {
                            switch (root.accountStatus) {
                            case "signed-out":
                                return i18n("Google opens in your default browser, where its password manager works. The widget only reads your subscription list.");
                            case "not-configured":
                                return i18n("Sign-in isn't set up on this computer yet: it needs a Google OAuth client (see the plasma-touchpad README).");
                            }
                            return "";
                        }
                        // Shown whenever not signed in; until an OAuth client
                        // is set up it is greyed out, with the reason above.
                        // (A disabled helpfulAction would be hidden.)
                        PlasmaComponents.Button {
                            Layout.alignment: Qt.AlignHCenter
                            visible: root.accountStatus === "signed-out" || root.accountStatus === "not-configured"
                            enabled: root.accountStatus === "signed-out"
                            icon.name: "user-identity"
                            text: i18n("Sign In to YouTube…")
                            onClicked: root.accountCall("signIn", () => root.checkAccount())
                        }
                    }
                }
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
                html: root.viewing ? root.playerPage(root.viewing.id) : ""
                // "Watch on YouTube" and the like go to the browser.
                externalPopups: true
                onBack: root.viewing = null
                // The player could not play the video here (e.g. 153, a
                // player configuration error, or 101/150, embedding turned
                // off by the owner): play it in the browser instead.
                onMessage: text => {
                    if (!text.startsWith("error:"))
                        return;
                    console.warn("Dashboard Videos: player", text, "- opening", root.viewing.link, "in the browser");
                    Qt.openUrlExternally(root.viewing.link);
                    root.viewing = null;
                }
            }
        }
    }
}
