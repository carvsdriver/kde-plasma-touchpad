#include "WindowMover.h"
#include "KWinScript.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDebug>

namespace PlasmaTP {


// KWin script (plugin) name of the window guard; see startGuard().
static constexpr const char *GUARD_SCRIPT = "plasma-touchpad-window-guard";

// DBus service/path we register so KWin scripts can call reportMovedWindow().
static constexpr const char *OUR_SERVICE = "org.kde.plasma.touchpad";
static constexpr const char *OUR_PATH = "/WindowMover";

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
    // A guard left running by a daemon that crashed while docked.
    stopGuard();
}

void WindowMover::reportMovedWindow(const QString &internalId)
{
    m_movedWindowIds.append(internalId);
}

void WindowMover::moveWindowsToTopScreen()
{
    startGuard();
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
                // Minimized windows lose their output reference in KWin 6.
                // Fall back to frameGeometry: eDP-2 occupies y >= 1125 in
                // KWin's logical coordinate space.
                var onEDP2;
                if (w.minimized) {
                    var g = w.frameGeometry;
                    onEDP2 = (g.y + g.height / 2 >= 1125);
                } else {
                    onEDP2 = (w.output && w.output.name === "eDP-2");
                }
                if (!onEDP2) continue;
                callDBus(
                    "org.kde.plasma.touchpad", "/WindowMover",
                    "org.kde.plasma.touchpad.WindowMover", "reportMovedWindow",
                    w.internalId.toString(), function() {}
                );
                if (w.minimized) {
                    // Briefly unminimize so sendClientToScreen records the new
                    // output, then re-minimize.  All ops are queued in one script
                    // execution so there is no visible flash.
                    w.minimized = false;
                    workspace.sendClientToScreen(w, outTop);
                    w.minimized = true;
                } else {
                    workspace.sendClientToScreen(w, outTop);
                    if (!firstMoved) firstMoved = w;
                }
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
    stopGuard();
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
                // Minimized windows lose their output reference in KWin 6.
                // Fall back to frameGeometry: eDP-1 occupies y < 1125.
                var onEDP1;
                if (w.minimized) {
                    var g = w.frameGeometry;
                    onEDP1 = (g.y + g.height / 2 < 1125);
                } else {
                    onEDP1 = (w.output && w.output.name === "eDP-1");
                }
                if (!onEDP1) continue;
                var id = w.internalId.toString();
                for (var j = 0; j < ids.length; j++) {
                    if (id === ids[j]) {
                        if (w.minimized) {
                            w.minimized = false;
                            workspace.sendClientToScreen(w, outBottom);
                            w.minimized = true;
                        } else {
                            workspace.sendClientToScreen(w, outBottom);
                            if (!firstMoved) firstMoved = w;
                        }
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

void WindowMover::startGuard()
{
    // Guarded: application windows and their dialogs, and plasmashell's
    // settings windows (a widget's settings dialog would open beside the
    // dashboard widget on eDP-2, often taller than the space there).  Not
    // guarded: this daemon's own surfaces, and panels, widget popups, OSDs
    // and the like (not normal or dialog windows, not moveable, or - for
    // plasmashell - not shown in the taskbar).
    //
    // A drag is only checked once it ends: fighting an interactive move
    // from a script makes the window jitter.  A window that lands on eDP-2
    // is sent to eDP-1, then moved up (and shortened if need be) so that no
    // part of it reaches below eDP-1.
    const QString script = QStringLiteral(R"JS(
        var TOP = "eDP-1";
        var BOTTOM = "eDP-2";

        function topOutput() {
            var out = workspace.screenAt({x: 900, y: 562});
            return out && out.name === TOP ? out : null;
        }

        // Only windows the user can move.  The daemon's own touchpad and
        // blackout surfaces (layer-shell) count as normal windows but are
        // not moveable; they are excluded by class as well, since moving
        // them would take the touchpad off eDP-2.
        function guarded(w) {
            if (!(w.normalWindow || w.dialog) || !w.moveable)
                return false;
            if (w.resourceClass === "plasma-touchpad-daemon")
                return false;
            // plasmashell (its settings windows report the class
            // "org.kde.plasmashell", its panels and desktops "plasmashell"):
            // settings windows have a taskbar entry; widget popups and the
            // like do not.
            if (w.resourceClass === "plasmashell" || w.resourceClass === "org.kde.plasmashell")
                return !w.skipTaskbar;
            return true;
        }

        function check(w) {
            if (w.move || w.resize || w.deleted)
                return;
            var top = topOutput();
            if (!top)
                return;
            // Minimized windows lose their output; go by geometry (eDP-2 is
            // below eDP-1).
            var g = w.frameGeometry;
            var onBottom = w.minimized ? (g.y + g.height / 2 >= top.geometry.y + top.geometry.height)
                                       : (w.output && w.output.name === BOTTOM);
            if (onBottom) {
                console.warn("plasma-touchpad guard: sending", w.caption, "back to", TOP);
                workspace.sendClientToScreen(w, top);
                g = w.frameGeometry;
            }
            if (w.fullScreen || w.minimized)
                return;
            // Fit vertically inside eDP-1: nothing below its bottom edge, and
            // nothing above its top (sendClientToScreen keeps the position
            // relative to the output, so a window dropped low on eDP-2 lands
            // partly above eDP-1).
            var area = top.geometry;
            var limit = area.y + area.height;
            if (g.y + g.height > limit || g.y < area.y) {
                var h = Math.min(g.height, area.height);
                var y = Math.min(Math.max(g.y, area.y), limit - h);
                w.frameGeometry = { x: g.x, y: y, width: g.width, height: h };
            }
        }

        function watch(w) {
            if (!guarded(w))
                return;
            w.interactiveMoveResizeFinished.connect(function () { check(w); });
            w.outputChanged.connect(function () { check(w); });
            check(w);
        }

        workspace.windowList().forEach(watch);
        workspace.windowAdded.connect(watch);
    )JS");
    PlasmaTP::runKWinScript(QLatin1String(GUARD_SCRIPT), script);
}

void WindowMover::stopGuard()
{
    unloadKWinScript(QLatin1String(GUARD_SCRIPT));
}

void WindowMover::showDesktopTopScreen()
{
    const QString script = QStringLiteral(R"JS(
        var outTop = workspace.screenAt({x: 900, y: 562});
        if (!outTop || outTop.name !== "eDP-1") {
            console.warn("plasma-touchpad showDesktop: eDP-1 not at expected position");
        } else {
            var wl = workspace.windowList();
            for (var i = 0; i < wl.length; i++) {
                var w = wl[i];
                if (w.normalWindow && w.output && w.output.name === "eDP-1" && !w.skipTaskbar)
                    w.minimized = true;
            }
        }
    )JS");
    runKWinScript(script);
}

void WindowMover::unminimizeTopScreen()
{
    // Minimized windows lose their output reference in KWin 6, so we cannot
    // filter by output here.  Unminimize all minimized normal windows instead —
    // this matches standard show-desktop toggle behaviour.
    const QString script = QStringLiteral(R"JS(
        var wl = workspace.windowList();
        for (var i = 0; i < wl.length; i++) {
            var w = wl[i];
            if (w.normalWindow && !w.skipTaskbar && w.minimized)
                w.minimized = false;
        }
    )JS");
    runKWinScript(script);
}

void WindowMover::runKWinScript(const QString &scriptContent)
{
    PlasmaTP::runKWinScript(QStringLiteral("plasma-touchpad-window-mover"), scriptContent);
}

} // namespace PlasmaTP
