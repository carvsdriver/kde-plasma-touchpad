#include "DockWatcher.h"

#include <QDir>
#include <QFile>
#include <QDebug>

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

namespace PlasmaTP {

// Name reported by the kbdock input device
static constexpr const char *KBDOCK_INPUT_NAME = "Lenovo Yoga Book 9 keyboard dock switch";

DockWatcher::DockWatcher(QObject *parent)
    : QObject(parent)
{
    // Debounce timer: position changes are only committed after 150 ms of
    // stability.  The kbdock driver fires multiple SW_TABLET_MODE events
    // during dock insertion; without debounce the overlay flickers.
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(150);
    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        setPosition(m_pendingPosition);
    });

    // Poll sysfs once at startup to get the current state.
    pollSysfs();

    // Poll sysfs every 1 s. The SW_TABLET_MODE evdev event only fires for
    // BKBD=2 (bottom-half) transitions; BKBD=1 (top-half) transitions are
    // silent on the evdev side, so sysfs polling is the sole detection path
    // for that position.  A 1 s interval is essentially free (single-byte
    // kernel attribute read) and gives sub-second detection for both positions.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DockWatcher::pollSysfs);
    timer->start(1'000);

    openEvdev();
}

DockWatcher::~DockWatcher()
{
    if (m_evdevFd >= 0)
        ::close(m_evdevFd);
}

DockPosition DockWatcher::readSysfs() const
{
    QFile f{QLatin1String(SYSFS_PATH)};
    if (!f.open(QIODevice::ReadOnly))
        return DockPosition::Detached;

    const QByteArray raw = f.readLine().trimmed();
    // Format: "N (name)" — take the leading digit
    if (raw.isEmpty())
        return DockPosition::Detached;

    switch (raw.at(0)) {
    case '1': return DockPosition::TopHalf;
    case '2': return DockPosition::BottomHalf;
    default:  return DockPosition::Detached;
    }
}

void DockWatcher::pollSysfs()
{
    m_pendingPosition = readSysfs();
    m_debounceTimer->start(); // restarts if already running
}

void DockWatcher::openEvdev()
{
    // Find the event node for the kbdock input device by scanning /dev/input
    const QDir dir(QStringLiteral("/dev/input"));
    const auto entries = dir.entryList(QStringList{QStringLiteral("event*")}, QDir::System);

    for (const QString &entry : entries) {
        const QString path = dir.filePath(entry);
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        char name[256] = {};
        ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (QLatin1String(name) == QLatin1String(KBDOCK_INPUT_NAME)) {
            m_evdevFd = fd;
            m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
            connect(m_notifier, &QSocketNotifier::activated,
                    this, [this](int fd){ evdevReadyRead(fd); });
            qDebug() << "DockWatcher: watching" << path;
            return;
        }
        ::close(fd);
    }

    qWarning() << "DockWatcher: kbdock input device not found;"
               << "falling back to sysfs polling only";
}

void DockWatcher::evdevReadyRead(int fd)
{
    input_event ev{};
    while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (ev.type == EV_SW && ev.code == SW_TABLET_MODE) {
            // SW_TABLET_MODE only tells us docked/undocked. Re-read sysfs
            // for the exact BKBD position immediately after the event.
            pollSysfs();
        }
    }
}

void DockWatcher::setPosition(DockPosition p)
{
    if (p == m_position)
        return;
    m_position = p;
    Q_EMIT dockChanged(p);
}

} // namespace PlasmaTP
