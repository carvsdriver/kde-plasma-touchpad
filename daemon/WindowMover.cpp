#include "WindowMover.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QTextStream>

namespace PlasmaTP {

static constexpr const char *KWIN_SERVICE = "org.kde.KWin";
static constexpr const char *KWIN_SCRIPTING_PATH = "/Scripting";
static constexpr const char *KWIN_SCRIPTING_IFACE = "org.kde.kwin.Scripting";
static constexpr const char *KWIN_SCRIPT_IFACE = "org.kde.kwin.Script";

// DBus service/path we register so KWin scripts can call reportMovedWindow().
static constexpr const char *OUR_SERVICE = "org.kde.plasma.touchpad";
static constexpr const char *OUR_PATH = "/WindowMover";

// Temporary file used to pass scripts to KWin (loadScript requires a file path).
static constexpr const char *SCRIPT_FILE = "/tmp/plasma-touchpad-kwin.js";

WindowMover::WindowMover(QObject *parent)
    : QObject(parent)
{
    // Register this object on the session bus so KWin scripts can reach us.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QLatin1String(OUR_SERVICE)))
        qWarning() << "WindowMover: could not register DBus service" << OUR_SERVICE
                   << "(already registered by another component — that's OK)";
    if (!bus.registerObject(QLatin1String(OUR_PATH), this,
                            QDBusConnection::ExportScriptableSlots))
        qWarning() << "WindowMover: could not register DBus object" << OUR_PATH;
}

void WindowMover::reportMovedWindow(const QString &internalId)
{
    m_movedWindowIds.append(internalId);
}

void WindowMover::moveWindowsToTopScreen()
{
    if (m_moved)
        return;

    m_movedWindowIds.clear();

    // KWin 6 script:
    //   1. Find the eDP-1 output object by looking for any window (normal or not)
    //      that is already on eDP-1 — workspace.outputs is unavailable in KWin 6.6.
    //   2. Move every normalWindow on eDP-2 to eDP-1.
    //   3. Report each moved window's internalId to us via callDBus so we can
    //      restore precisely.
    // KWin 6.6 scripting notes:
    //  - workspace.outputs is undefined; use screenAt({x,y}) to get output objects
    //  - w.output is read-only; use workspace.sendClientToScreen(w, outputObj) to move
    //  - w.internalId is QUuid, not string; call .toString() before passing over DBus
    //  - Qt is not defined; use plain JS objects {x,y} for points
    //  - console.warn() goes to kwin journal; print() is suppressed
    //  - Use y=562 (center of eDP-1, height 1125) and y=1687 (center of eDP-2)
    const QString script = QStringLiteral(R"JS(
        var outTop = workspace.screenAt({x: 900, y: 562});
        if (!outTop || outTop.name !== "eDP-1") {
            console.warn("plasma-touchpad WindowMover: eDP-1 not at expected position");
        } else {
            var savedActive = workspace.activeWindow;
            // Minimize the active window before moving so KWin issues
            // wl_keyboard::leave before the output transition.  Without this,
            // sendClientToScreen (which is async) races with the active-window
            // assignment and the window lands on eDP-1 without a fresh
            // wl_keyboard::enter, leaving it visually focused but unresponsive.
            // All three operations (minimize, move, restore) are queued in one
            // script execution before any frame renders, so there is no
            // visible animation.
            var needsRestore = (savedActive && savedActive.normalWindow &&
                                savedActive.output && savedActive.output.name === "eDP-2");
            if (needsRestore) savedActive.minimized = true;
            var firstMoved = null;
            var wl = workspace.windowList();
            for (var i = 0; i < wl.length; i++) {
                var w = wl[i];
                if (!w.normalWindow) continue;
                if (!w.output || w.output.name !== "eDP-2") continue;
                callDBus(
                    "org.kde.plasma.touchpad", "/WindowMover",
                    "org.kde.plasma.touchpad.WindowMover", "reportMovedWindow",
                    w.internalId.toString(), function() {}
                );
                workspace.sendClientToScreen(w, outTop);
                if (!firstMoved) firstMoved = w;
            }
            if (needsRestore) savedActive.minimized = false;
            if (savedActive && savedActive.normalWindow && firstMoved) {
                workspace.activeWindow = savedActive;
            } else if (firstMoved) {
                workspace.activeWindow = firstMoved;
            }
        }
    )JS");

    runKWinScript(script);

    // Allow the async callDBus messages from the KWin script to arrive.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    qDebug() << "WindowMover: moved" << m_movedWindowIds.size()
             << "windows to eDP-1:" << m_movedWindowIds;
    m_moved = true;
}


void WindowMover::restore()
{
    if (!m_moved)
        return;

    if (m_movedWindowIds.isEmpty()) {
        m_moved = false;
        return;
    }

    // Build a JS array literal of the IDs we recorded at move time.
    // Only restore windows that are currently on eDP-1 AND were moved by us.
    QStringList quoted;
    for (const QString &id : m_movedWindowIds)
        quoted.append(QLatin1Char('"') + id + QLatin1Char('"'));
    const QString idsLiteral = QLatin1Char('[') + quoted.join(QLatin1Char(',')) + QLatin1Char(']');

    const QString script = QStringLiteral(R"JS(
        var ids = %1;
        var outBottom = workspace.screenAt({x: 900, y: 1687});
        if (!outBottom || outBottom.name !== "eDP-2") {
            console.warn("plasma-touchpad WindowMover: eDP-2 not at expected position for restore");
        } else {
            var savedActive = workspace.activeWindow;
            var firstMoved = null;
            var wl = workspace.windowList();
            for (var i = 0; i < wl.length; i++) {
                var w = wl[i];
                if (!w.normalWindow) continue;
                if (!w.output || w.output.name !== "eDP-1") continue;
                var id = w.internalId.toString();
                for (var j = 0; j < ids.length; j++) {
                    if (id === ids[j]) {
                        workspace.sendClientToScreen(w, outBottom);
                        if (!firstMoved) firstMoved = w;
                        break;
                    }
                }
            }
            if (savedActive && savedActive.normalWindow && firstMoved) {
                workspace.activeWindow = savedActive;
            } else if (firstMoved) {
                workspace.activeWindow = firstMoved;
            }
        }
    )JS").arg(idsLiteral);

    runKWinScript(script);
    qDebug() << "WindowMover: restored" << m_movedWindowIds.size() << "windows to eDP-2";
    m_movedWindowIds.clear();
    m_moved = false;
}

void WindowMover::runKWinScript(const QString &scriptContent)
{
    // KWin 6 loadScript requires a file path, not inline content.
    {
        QFile f{QLatin1String(SCRIPT_FILE)};
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning() << "WindowMover: cannot write script file" << SCRIPT_FILE;
            return;
        }
        QTextStream((&f)) << scriptContent;
    }

    QDBusInterface kwin(QLatin1String(KWIN_SERVICE),
                        QLatin1String(KWIN_SCRIPTING_PATH),
                        QLatin1String(KWIN_SCRIPTING_IFACE),
                        QDBusConnection::sessionBus());

    if (!kwin.isValid()) {
        qWarning() << "WindowMover: KWin scripting DBus not available";
        return;
    }

    // loadScript returns the script ID (or the ID of an already-loaded copy).
    // Unload first so the fresh file content is guaranteed to run.
    kwin.call(QStringLiteral("unloadScript"), QLatin1String("plasma-touchpad-window-mover"));
    QDBusReply<int> loadReply = kwin.call(
        QStringLiteral("loadScript"),
        QLatin1String(SCRIPT_FILE),
        QStringLiteral("plasma-touchpad-window-mover"));

    if (!loadReply.isValid() || loadReply.value() < 0) {
        qWarning() << "WindowMover: loadScript failed:" << loadReply.error().message();
        return;
    }

    const QString scriptPath = QStringLiteral("/Scripting/Script") + QString::number(loadReply.value());
    QDBusInterface scriptIface(QLatin1String(KWIN_SERVICE), scriptPath,
                               QLatin1String(KWIN_SCRIPT_IFACE),
                               QDBusConnection::sessionBus());
    scriptIface.call(QStringLiteral("run"));
}

} // namespace PlasmaTP
