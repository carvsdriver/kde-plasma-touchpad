#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>

namespace PlasmaTP {

// Keyboard dock position, mirroring BKBD EC field values.
enum class DockPosition {
    Detached   = 0,
    TopHalf    = 1,   // keyboard on top half of bottom screen
    BottomHalf = 2,   // keyboard on bottom half of bottom screen
};

/*
 * DockWatcher monitors two sources:
 *
 *  1. The kbdock sysfs attribute (keyboard_position) — polled on a timer to
 *     catch state at startup and after resume.
 *
 *  2. The SW_TABLET_MODE input device created by lenovo-yb9-kbdock — watched
 *     via evdev for real-time edge notifications.
 *
 * Emits dockChanged(position) whenever the keyboard attachment state changes.
 */
class DockWatcher : public QObject
{
    Q_OBJECT

public:
    static constexpr const char *SYSFS_PATH =
        "/sys/bus/wmi/devices/806BD2A2-177B-481D-BFB5-3BA0BB4A2285-11/keyboard_position";

    explicit DockWatcher(QObject *parent = nullptr);
    ~DockWatcher() override;

    DockPosition position() const { return m_position; }
    bool isDocked() const { return m_position != DockPosition::Detached; }

Q_SIGNALS:
    void dockChanged(DockPosition position);

private Q_SLOTS:
    void pollSysfs();
    void evdevReadyRead(int fd);

private:
    void openEvdev();
    DockPosition readSysfs() const;
    void setPosition(DockPosition p);

    DockPosition m_position = DockPosition::Detached;
    DockPosition m_pendingPosition = DockPosition::Detached;
    int m_evdevFd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QTimer *m_debounceTimer = nullptr;
};

} // namespace PlasmaTP
