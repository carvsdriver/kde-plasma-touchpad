import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM
import org.kde.plasma.workspace.dbus as DBus

KCM.SimpleKCM {
    id: page

    property alias cfg_title: titleField.text
    property alias cfg_hideShorts: hideShorts.checked
    property alias cfg_maxItems: maxItems.value
    property alias cfg_refreshMinutes: refreshMinutes.value
    property string cfg_openIn: "widget"


    // The touchpad daemon's YouTube account: "signed-in", "signed-out",
    // "signing-in", "not-configured", or "unavailable" (daemon not running).
    property string accountStatus: ""
    property string accountName: ""

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
        }, () => {
            if (done)
                page.accountStatus = "unavailable";
        });
    }

    // Sign-in finishes in the browser, so keep checking while this page is
    // open.
    Timer {
        interval: 2000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: page.accountCall("status", status => {
            page.accountStatus = status;
            if (status === "signed-in")
                page.accountCall("accountName", name => page.accountName = name);
        })
    }

    Kirigami.FormLayout {
        QQC2.TextField {
            id: titleField
            Kirigami.FormData.label: i18n("Heading:")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("YouTube account:")
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                text: {
                    switch (page.accountStatus) {
                    case "signed-in":
                        return page.accountName ? i18n("Signed in as %1", page.accountName) : i18n("Signed in");
                    case "signing-in":
                        return i18n("Finish signing in in your browser…");
                    case "signed-out":
                        return i18n("Not signed in");
                    case "not-configured":
                        return i18n("Sign-in is not set up on this computer");
                    case "unavailable":
                        return i18n("The touchpad service is not running");
                    }
                    return "";
                }
            }
            QQC2.Button {
                visible: page.accountStatus !== "unavailable" && page.accountStatus !== ""
                enabled: page.accountStatus === "signed-in" || page.accountStatus === "signed-out"
                icon.name: page.accountStatus === "signed-in" ? "system-log-out" : "user-identity"
                text: page.accountStatus === "signed-in" ? i18n("Sign Out") : i18n("Sign In to YouTube…")
                onClicked: page.accountCall(page.accountStatus === "signed-in" ? "signOut" : "signIn")
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            visible: page.accountStatus === "signed-out"
            text: i18n("Signing in opens Google in your default browser, so its password manager works there. The widget only reads your subscription list.")
        }

        QQC2.CheckBox {
            id: hideShorts
            text: i18n("Leave out Shorts")
        }

        QQC2.SpinBox {
            id: maxItems
            Kirigami.FormData.label: i18n("Videos to show:")
            from: 4
            to: 200
        }

        QQC2.SpinBox {
            id: refreshMinutes
            Kirigami.FormData.label: i18n("Refresh every:")
            from: 5
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
            text: i18n("Sites that need you to sign in work best in the browser, where its password manager and extensions are available. If a video can't be played here, it opens in the browser instead.")
        }
    }
}
