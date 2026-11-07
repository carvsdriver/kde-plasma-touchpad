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

    // Every video fetched from the subscribed channels, newest first, each
    // tagged with its channelId.  `videos` is what the grid shows: these,
    // less hidden channels, narrowed by the search.
    property var allVideos: []
    property var videos: []
    // The subscribed channels, [{ id, title }], sorted by title.
    property var channels: []
    // The header's search text.
    property string searchText: ""
    // The channel chooser is shown instead of the grid.
    property bool choosingChannels: false
    // Channel IDs left out of the grid, as an object used as a set.
    readonly property var hiddenSet: {
        const set = {};
        (Plasmoid.configuration.hiddenChannels || "").split(",").forEach(id => {
            if (id)
                set[id] = true;
        });
        return set;
    }
    property int failedFeeds: 0
    // How many videos the grid shows: a batch of maxItems, one more batch
    // for each "Load More".  Back to one batch when the search or the
    // channels change.
    property int shownCount: Plasmoid.configuration.maxItems
    // More of the fetched videos match than the grid shows.
    property bool hasMore: false
    property int matchCount: 0
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

    // Open a subscribed channel's YouTube page: in the widget, or in the
    // default browser if so set.  A channel has no video id, so the viewer
    // loads the page itself.
    function openChannel(channel) {
        const link = "https://www.youtube.com/channel/" + channel.id;
        if (Plasmoid.configuration.openIn === "browser")
            Qt.openUrlExternally(link);
        else
            viewing = { title: channel.title, link: link };
    }

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
                allVideos = [];
                videos = [];
                channels = [];
                choosingChannels = false;
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
            let list = [];
            try { list = JSON.parse(json); } catch (e) {}
            list = list.filter(c => c.id);
            list.sort((a, b) => String(a.title).localeCompare(String(b.title)));
            channels = list;
            if (!list.length) {
                allVideos = [];
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
            load(list.map(c => c.id));
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
            const channelId = channels[next++];
            get("https://www.youtube.com/feeds/videos.xml?channel_id=" + channelId, xhr => {
                const list = xhr.status === 200 ? Feed.parse(xhr.responseXML) : [];
                if (!list.length)
                    failed++;
                list.forEach(v => v.channelId = channelId);
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
        allVideos = list;
        failedFeeds = failed;
        now = Date.now();
        loading = false;
        applyFilter();
    }

    // Fill the grid from allVideos: hidden channels left out, then narrowed
    // to the search.  A search looks through every fetched video, not only
    // the newest few.
    function applyFilter() {
        const query = searchText.trim().toLowerCase();
        let list = allVideos.filter(v => !hiddenSet[v.channelId]);
        const shown = list.length;
        if (query)
            list = list.filter(v => (v.title + " " + (v.source || "")).toLowerCase().indexOf(query) >= 0);
        matchCount = list.length;
        videos = list.slice(0, shownCount);
        hasMore = list.length > videos.length;
        if (videos.length)
            error = "";
        else if (query && shown)
            error = i18n("No videos match “%1”.", searchText.trim());
        else if (allVideos.length && !shown)
            error = i18n("Every channel is hidden. Choose channels from the menu.");
        else
            error = failedFeeds ? i18n("Could not load the channel feeds.") : i18n("No videos.");
    }

    // Show the next batch of fetched videos.
    function loadMore() {
        shownCount += Plasmoid.configuration.maxItems;
        applyFilter();
    }

    // Back to the first batch, then fill the grid.
    function resetAndFilter() {
        shownCount = Plasmoid.configuration.maxItems;
        applyFilter();
    }

    // Show or hide one channel's videos.
    function setChannelHidden(id, hide) {
        const set = Object.assign({}, hiddenSet);
        if (hide)
            set[id] = true;
        else
            delete set[id];
        Plasmoid.configuration.hiddenChannels = Object.keys(set).join(",");
    }

    // Show every channel, or hide them all.
    function setAllChannelsHidden(hide) {
        Plasmoid.configuration.hiddenChannels = hide ? channels.map(c => c.id).join(",") : "";
    }

    Component.onCompleted: checkAccount()

    Connections {
        target: Plasmoid.configuration
        function onHideShortsChanged() { root.refresh(); }
        function onMaxItemsChanged() { root.resetAndFilter(); }
        function onHiddenChannelsChanged() { root.resetAndFilter(); }
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

    onSearchTextChanged: resetAndFilter()

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
            visible: root.viewing === null && !root.choosingChannels
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents.ToolButton {
                    icon.name: "application-menu"
                    text: i18n("Choose Channels")
                    display: PlasmaComponents.AbstractButton.IconOnly
                    enabled: root.channels.length > 0
                    onClicked: root.choosingChannels = true
                    PlasmaComponents.ToolTip.text: text
                    PlasmaComponents.ToolTip.visible: hovered
                }
                Kirigami.Heading {
                    // Gives way to the search field when space is short.
                    Layout.fillWidth: true
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 3
                    level: 3
                    text: Plasmoid.configuration.title
                    elide: Text.ElideRight
                }
                PlasmaExtras.SearchField {
                    Layout.fillWidth: true
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 14
                    enabled: root.accountStatus === "signed-in"
                    placeholderText: i18n("Search videos…")
                    onTextChanged: root.searchText = text
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

            // The grid, with a back-to-top button floating over its bottom
            // right corner once it's scrolled down.
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                PlasmaComponents.ScrollView {
                    anchors.fill: parent

                    GridView {
                        id: grid
                        clip: true
                        model: root.videos

                        readonly property int columns: Math.max(1, Math.floor(width / (Kirigami.Units.gridUnit * 11)))
                        cellWidth: Math.floor(width / columns)
                        cellHeight: Math.round((cellWidth - Kirigami.Units.smallSpacing * 2) * 9 / 16)
                                    + Kirigami.Units.gridUnit * 3.5

                        // Below the last video: the next batch, while there is
                        // one.
                        footer: Item {
                            width: grid.width
                            height: root.hasMore ? loadMoreButton.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
                            visible: root.hasMore
                            PlasmaComponents.Button {
                                id: loadMoreButton
                                anchors.centerIn: parent
                                icon.name: "go-down"
                                text: i18n("Load More (%1 of %2 shown)", root.videos.length, root.matchCount)
                                // A new list resets the grid to the top; stay
                                // where the user was, now and once the grid has
                                // laid out the new batch.
                                onClicked: {
                                    const y = grid.contentY;
                                    root.loadMore();
                                    grid.contentY = y;
                                    Qt.callLater(() => grid.contentY = y);
                                }
                            }
                        }

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
                            // (A disabled helpfulAction would be hidden.)  While
                            // a sign-in is pending it starts over, in case its
                            // browser tab was closed.
                            PlasmaComponents.Button {
                                Layout.alignment: Qt.AlignHCenter
                                visible: root.accountStatus === "signed-out" || root.accountStatus === "not-configured"
                                         || root.accountStatus === "signing-in"
                                enabled: root.accountStatus === "signed-out" || root.accountStatus === "signing-in"
                                icon.name: root.accountStatus === "signing-in" ? "view-refresh" : "user-identity"
                                text: root.accountStatus === "signing-in" ? i18n("Try Again") : i18n("Sign In to YouTube…")
                                onClicked: root.accountCall("signIn", () => root.checkAccount())
                            }
                        }
                    }
                }

                PlasmaComponents.RoundButton {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    // Clear of the scroll bar.
                    anchors.rightMargin: Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing
                    anchors.bottomMargin: Kirigami.Units.largeSpacing
                    icon.name: "go-top"
                    text: i18n("Back to Top")
                    display: PlasmaComponents.AbstractButton.IconOnly
                    // Shown once the first row is out of sight.
                    readonly property bool wanted: grid.contentY - grid.originY > grid.cellHeight
                    opacity: wanted ? 1 : 0
                    visible: opacity > 0
                    Behavior on opacity {
                        NumberAnimation { duration: Kirigami.Units.shortDuration }
                    }
                    onClicked: scrollToTop.start()
                    PlasmaComponents.ToolTip.text: text
                    PlasmaComponents.ToolTip.visible: hovered

                    NumberAnimation {
                        id: scrollToTop
                        target: grid
                        property: "contentY"
                        to: grid.originY
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.OutCubic
                    }
                }
            }
        }

        // The channel chooser, opened from the header's menu button: a check
        // box per subscribed channel, ticked when its videos are shown.
        ColumnLayout {
            anchors.fill: parent
            visible: root.viewing === null && root.choosingChannels
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                PlasmaComponents.ToolButton {
                    icon.name: "go-previous"
                    text: i18n("Back")
                    display: PlasmaComponents.AbstractButton.IconOnly
                    onClicked: root.choosingChannels = false
                    PlasmaComponents.ToolTip.text: text
                    PlasmaComponents.ToolTip.visible: hovered
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: {
                        const hidden = root.channels.filter(c => root.hiddenSet[c.id]).length;
                        return i18n("Channels (%1 of %2 shown)", root.channels.length - hidden, root.channels.length);
                    }
                    elide: Text.ElideRight
                }
                PlasmaComponents.ToolButton {
                    text: i18n("All")
                    onClicked: root.setAllChannelsHidden(false)
                    PlasmaComponents.ToolTip.text: i18n("Show every channel")
                    PlasmaComponents.ToolTip.visible: hovered
                }
                PlasmaComponents.ToolButton {
                    text: i18n("None")
                    onClicked: root.setAllChannelsHidden(true)
                    PlasmaComponents.ToolTip.text: i18n("Hide every channel")
                    PlasmaComponents.ToolTip.visible: hovered
                }
            }

            PlasmaExtras.SearchField {
                id: channelSearch
                Layout.fillWidth: true
                placeholderText: i18n("Find a channel…")
            }

            PlasmaComponents.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    clip: true
                    // Depends on the channel list and the filter only, so
                    // ticking a box doesn't reset the scroll position.
                    model: {
                        const query = channelSearch.text.trim().toLowerCase();
                        return query ? root.channels.filter(c => String(c.title).toLowerCase().indexOf(query) >= 0)
                                     : root.channels;
                    }
                    delegate: PlasmaComponents.CheckDelegate {
                        id: channelDelegate
                        required property var modelData
                        width: ListView.view.width
                        text: modelData.title
                        checked: !root.hiddenSet[modelData.id]
                        // The channel's avatar beside its name; its initials
                        // until (or unless) the picture loads.
                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            // (Kirigami.Avatar is in Kirigami Addons, not
                            // Kirigami, so this is a round image over a
                            // circle with the channel's initial.)
                            Rectangle {
                                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                radius: width / 2
                                color: Kirigami.Theme.alternateBackgroundColor
                                PlasmaComponents.Label {
                                    anchors.centerIn: parent
                                    text: String(channelDelegate.modelData.title || "?").charAt(0).toUpperCase()
                                    textFormat: Text.PlainText
                                }
                                Kirigami.ShadowedImage {
                                    anchors.fill: parent
                                    radius: width / 2
                                    color: "transparent"
                                    source: channelDelegate.modelData.icon || ""
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                }
                            }
                            PlasmaComponents.Label {
                                Layout.fillWidth: true
                                text: channelDelegate.text
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                            // Takes the tap itself, so it doesn't toggle the
                            // channel's check box.
                            PlasmaComponents.ToolButton {
                                icon.name: "go-next"
                                text: i18n("Open Channel")
                                display: PlasmaComponents.AbstractButton.IconOnly
                                onClicked: root.openChannel(channelDelegate.modelData)
                                PlasmaComponents.ToolTip.text: text
                                PlasmaComponents.ToolTip.visible: hovered
                            }
                            // The check box is drawn over the right end of
                            // the content; keep clear of it, as the stock
                            // content item does.
                            Item {
                                Layout.preferredWidth: channelDelegate.indicator
                                    ? channelDelegate.indicator.width + channelDelegate.spacing : 0
                            }
                        }
                        onToggled: {
                            root.setChannelHidden(modelData.id, !checked);
                            // Ticking breaks the binding; restore it so All
                            // and None still update this box.
                            checked = Qt.binding(() => !root.hiddenSet[modelData.id]);
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
                html: root.viewing?.id ? root.playerPage(root.viewing.id) : ""
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
