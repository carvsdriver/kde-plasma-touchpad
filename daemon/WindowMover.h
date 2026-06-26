#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace PlasmaTP {

/*
 * WindowMover moves application windows between outputs when the touchpad
 * overlay activates.
 *
 * KWin 6 scripting notes:
 *   - workspace.windowList()    — all windows (replaces KWin 5 workspace.clientList())
 *   - workspace.windows         — undefined in KWin 6.6; use windowList()
 *   - workspace.outputs         — undefined in KWin 6.6; obtain output objects from
 *                                  existing windows via w.output
 *   - console.warn()            — goes to kwin_wayland journal; print() is suppressed
 *
 * Window ID tracking:
 *   KWin scripts cannot return values, so the move script calls back into the
 *   daemon via callDBus("org.kde.plasma.touchpad", "/WindowMover", ..., "reportMovedWindow")
 *   for each window it relocates.  WindowMover registers that DBus path on the
 *   session bus so the slot is reachable.
 */
class WindowMover : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasma.touchpad.WindowMover")

public:
    explicit WindowMover(QObject *parent = nullptr);

    // Move all normal windows from eDP-2 to eDP-1.
    void moveWindowsToTopScreen();
    // Move only the windows we previously relocated back to eDP-2.
    void restore();
    // Minimize all normal windows on eDP-1 only (show desktop, top screen).
    void showDesktopTopScreen();
    // Unminimize all normal windows on eDP-1 (restore after show desktop).
    void unminimizeTopScreen();

public Q_SLOTS:
    // Called by the KWin move script via callDBus for each window it moves.
    Q_SCRIPTABLE void reportMovedWindow(const QString &internalId);

private:
    void runKWinScript(const QString &scriptContent);

    QStringList m_movedWindowIds;
    bool m_moved = false;
};

} // namespace PlasmaTP
