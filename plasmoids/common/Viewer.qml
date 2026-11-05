// In-widget viewer for a dashboard item: a back button, the title, a button
// to open the item in the default browser, and a web view.  Shared by the
// dashboard widgets.
import QtQuick
import QtQuick.Layouts
import QtWebEngine
import org.kde.plasma.components as PlasmaComponents
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: viewer

    property string title
    // The item's own address: loaded unless `html` is given, and what the
    // browser button opens.
    property url pageUrl
    // Page content to show instead of loading pageUrl (e.g. a video player),
    // with the address it is treated as coming from.
    property string html
    property url baseUrl: "https://plasma-touchpad.localhost/"
    // Open links that ask for a new window in the default browser, instead
    // of in this view.
    property bool externalPopups: false

    signal back()
    // A page's console.log("plasma-touchpad:<text>"), e.g. a video player
    // reporting an error.
    signal message(string text)

    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        PlasmaComponents.ToolButton {
            icon.name: "go-previous"
            text: i18n("Back")
            display: PlasmaComponents.AbstractButton.IconOnly
            onClicked: view.canGoBack && !viewer.html ? view.goBack() : viewer.back()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
        }
        PlasmaComponents.Label {
            Layout.fillWidth: true
            text: viewer.html ? viewer.title : (view.title || viewer.title)
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        PlasmaComponents.BusyIndicator {
            visible: view.loading
            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
            Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
        }
        PlasmaComponents.ToolButton {
            icon.name: "internet-web-browser"
            text: i18n("Open in browser")
            display: PlasmaComponents.AbstractButton.IconOnly
            onClicked: {
                Qt.openUrlExternally(viewer.html ? viewer.pageUrl : view.url);
                viewer.back();
            }
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
        }
        PlasmaComponents.ToolButton {
            icon.name: "window-close"
            text: i18n("Close")
            display: PlasmaComponents.AbstractButton.IconOnly
            onClicked: viewer.back()
            PlasmaComponents.ToolTip.text: text
            PlasmaComponents.ToolTip.visible: hovered
        }
    }

    // Cookies and site settings persist between views (and dashboard
    // sessions), separately from any browser.
    WebEngineProfilePrototype {
        id: profile
        storageName: "plasma-touchpad-dashboard"
    }

    WebEngineView {
        id: view
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Videos start without a second tap.
        settings.playbackRequiresUserGesture: false

        onJavaScriptConsoleMessage: (level, message, lineNumber, sourceID) => {
            const prefix = "plasma-touchpad:";
            if (message.startsWith(prefix))
                viewer.message(message.substring(prefix.length));
        }

        onNewWindowRequested: request => {
            if (viewer.externalPopups)
                Qt.openUrlExternally(request.requestedUrl);
            else
                view.url = request.requestedUrl;
        }
        // A video's full-screen button fills the widget.
        onFullScreenRequested: request => request.accept()

        Component.onCompleted: {
            // Not a "profile: profile.instance()" binding: that is evaluated
            // before the prototype has made its profile, and setting a null
            // profile crashes plasmashell (QQuickWebEngineView::setProfile).
            // Set it here, before anything loads; without it the view keeps
            // the default profile.
            const shared = profile.instance();
            if (shared)
                view.profile = shared;
            if (viewer.html)
                loadHtml(viewer.html, viewer.baseUrl);
            else
                url = viewer.pageUrl;
        }
    }
}
