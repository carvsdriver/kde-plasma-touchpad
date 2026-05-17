#include "TouchInputHandler.h"

#include <QDir>
#include <QDebug>
#include <QDBusConnection>
#include <QDBusMessage>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>

namespace PlasmaTP {

static constexpr const char *TOUCH_DEVICE_NAME =
    "INGENIC Gadget Serial and keyboard Touchscreen Bottom";

static constexpr double BUTTON_STRIP_Y = 0.88;
// Per-frame raw-unit jitter floor: deltas smaller than this are sensor noise when
// the finger is nearly stationary and are discarded rather than accumulated.
static constexpr int JITTER_THRESHOLD_RAW = 5;
static constexpr int TAP_THRESHOLD_RAW = 400;

static void uinputSetBit(int fd, int request, int bit)
{
    ::ioctl(fd, request, bit);
}

// ─── ctor/dtor ──────────────────────────────────────────────────────────────

TouchInputHandler::TouchInputHandler(QObject *parent)
    : QObject(parent)
{
}

TouchInputHandler::~TouchInputHandler()
{
    stop();
}

void TouchInputHandler::setTouchpadRect(const QRectF &rect)
{
    m_touchpadRect = rect;
}

// ─── lifecycle ──────────────────────────────────────────────────────────────

bool TouchInputHandler::start()
{
    if (m_touchFd >= 0)
        return true;
    if (!openTouchscreen() || !openUinput()) {
        closeFds();
        return false;
    }
    m_notifier = new QSocketNotifier(m_touchFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated,
            this, [this](int fd){ touchReadyRead(fd); });
    return true;
}

void TouchInputHandler::stop()
{
    delete m_notifier;
    m_notifier = nullptr;
    closeFds();
}

void TouchInputHandler::closeFds()
{
    if (m_touchFd >= 0) {
        ::ioctl(m_touchFd, EVIOCGRAB, 0);
        ::close(m_touchFd);
        m_touchFd = -1;
    }
    if (m_uinputFd >= 0) {
        ::ioctl(m_uinputFd, UI_DEV_DESTROY);
        ::close(m_uinputFd);
        m_uinputFd = -1;
    }
}

// ─── device discovery ───────────────────────────────────────────────────────

bool TouchInputHandler::openTouchscreen()
{
    const QDir dir(QStringLiteral("/dev/input"));
    const auto entries = dir.entryList(QStringList{QStringLiteral("event*")}, QDir::System);

    for (const QString &entry : entries) {
        const QString path = dir.filePath(entry);
        int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;

        char name[256] = {};
        ::ioctl(fd, EVIOCGNAME(sizeof(name)), name);

        if (QLatin1String(name) == QLatin1String(TOUCH_DEVICE_NAME)) {
            input_absinfo absX{}, absY{};
            ::ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &absX);
            ::ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &absY);
            m_absXMin = absX.minimum; m_absXMax = absX.maximum;
            m_absYMin = absY.minimum; m_absYMax = absY.maximum;

            if (::ioctl(fd, EVIOCGRAB, 1) < 0)
                qWarning() << "TouchInputHandler: EVIOCGRAB failed";

            m_touchFd = fd;
            qDebug() << "TouchInputHandler: using" << path
                     << "axis X" << m_absXMin << "-" << m_absXMax
                     << "Y" << m_absYMin << "-" << m_absYMax;
            return true;
        }
        ::close(fd);
    }

    qWarning() << "TouchInputHandler: bottom touchscreen not found";
    return false;
}

bool TouchInputHandler::openUinput()
{
    m_uinputFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_uinputFd < 0) {
        qWarning() << "TouchInputHandler: cannot open /dev/uinput:" << strerror(errno);
        return false;
    }

    uinputSetBit(m_uinputFd, UI_SET_EVBIT, EV_KEY);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_LEFT);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_RIGHT);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, KEY_LEFTCTRL);
    uinputSetBit(m_uinputFd, UI_SET_EVBIT, EV_REL);
    uinputSetBit(m_uinputFd, UI_SET_RELBIT, REL_X);
    uinputSetBit(m_uinputFd, UI_SET_RELBIT, REL_Y);
    uinputSetBit(m_uinputFd, UI_SET_RELBIT, REL_WHEEL);
    uinputSetBit(m_uinputFd, UI_SET_RELBIT, REL_WHEEL_HI_RES);

    uinput_setup usetup{};
    usetup.id.bustype = BUS_VIRTUAL;
    usetup.id.vendor = 0x1;
    usetup.id.product = 0x1;
    ::strncpy(usetup.name, "Plasma Touchpad", UINPUT_MAX_NAME_SIZE);

    if (::ioctl(m_uinputFd, UI_DEV_SETUP, &usetup) < 0 ||
        ::ioctl(m_uinputFd, UI_DEV_CREATE) < 0) {
        qWarning() << "TouchInputHandler: uinput pointer device creation failed";
        return false;
    }

    qDebug() << "TouchInputHandler: uinput virtual pointer created";
    return true;
}

// ─── event processing ───────────────────────────────────────────────────────

void TouchInputHandler::touchReadyRead(int fd)
{
    input_event ev{};
    while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        if (m_pendingDrop) {
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                m_pendingDrop = false;
                recoverFromDrop();
            }
            continue;
        }

        switch (ev.type) {

        case EV_ABS:
            switch (ev.code) {
            case ABS_MT_SLOT:
                if (ev.value >= 0 && ev.value < MAX_SLOTS)
                    m_currentSlot = ev.value;
                break;

            case ABS_MT_TRACKING_ID:
                if (ev.value == -1) {
                    handleFingerLift(m_currentSlot);
                    m_slots[m_currentSlot].active = false;
                    m_slots[m_currentSlot].firstMove = true;
                    m_slots[m_currentSlot].inButtonStrip = false;
                    m_slots[m_currentSlot].buttonIndex = -1;
                } else {
                    m_slots[m_currentSlot].active = true;
                    m_slots[m_currentSlot].id = ev.value;
                    m_slots[m_currentSlot].firstMove = true;
                    m_slots[m_currentSlot].inButtonStrip = false;
                    m_slots[m_currentSlot].buttonIndex = -1;
                }
                break;

            case ABS_MT_POSITION_X:
                if (m_slots[m_currentSlot].firstMove)
                    m_slots[m_currentSlot].startX = ev.value;
                m_slots[m_currentSlot].prevX = m_slots[m_currentSlot].firstMove
                    ? ev.value : m_slots[m_currentSlot].x;
                m_slots[m_currentSlot].x = ev.value;
                break;

            case ABS_MT_POSITION_Y:
                if (m_slots[m_currentSlot].firstMove)
                    m_slots[m_currentSlot].startY = ev.value;
                m_slots[m_currentSlot].prevY = m_slots[m_currentSlot].firstMove
                    ? ev.value : m_slots[m_currentSlot].y;
                m_slots[m_currentSlot].y = ev.value;
                break;
            }
            break;

        case EV_SYN:
            if (ev.code == SYN_REPORT)
                processSyncReport();
            else if (ev.code == SYN_DROPPED)
                m_pendingDrop = true;
            break;

        default:
            break;
        }
    }
}

void TouchInputHandler::recoverFromDrop()
{
    struct {
        __u32 code;
        __s32 values[MAX_SLOTS];
    } req;

    req.code = ABS_MT_TRACKING_ID;
    if (::ioctl(m_touchFd, EVIOCGMTSLOTS(sizeof(req)), &req) < 0) {
        for (int i = 0; i < MAX_SLOTS; ++i)
            m_slots[i] = Slot{};
    } else {
        for (int i = 0; i < MAX_SLOTS; ++i) {
            const bool nowActive = (req.values[i] != -1);
            if (m_slots[i].active && !nowActive) {
                m_slots[i].active = false;
                m_slots[i].firstMove = true;
            } else if (!m_slots[i].active && nowActive) {
                m_slots[i].active = true;
                m_slots[i].id = req.values[i];
                m_slots[i].firstMove = true;
            } else if (m_slots[i].active && nowActive) {
                m_slots[i].id = req.values[i];
                m_slots[i].firstMove = true;
            }
        }
        req.code = ABS_MT_POSITION_X;
        if (::ioctl(m_touchFd, EVIOCGMTSLOTS(sizeof(req)), &req) == 0) {
            for (int i = 0; i < MAX_SLOTS; ++i)
                if (m_slots[i].active) { m_slots[i].x = req.values[i]; m_slots[i].prevX = req.values[i]; }
        }
        req.code = ABS_MT_POSITION_Y;
        if (::ioctl(m_touchFd, EVIOCGMTSLOTS(sizeof(req)), &req) == 0) {
            for (int i = 0; i < MAX_SLOTS; ++i)
                if (m_slots[i].active) { m_slots[i].y = req.values[i]; m_slots[i].prevY = req.values[i]; }
        }
    }

    input_absinfo slotInfo{};
    if (::ioctl(m_touchFd, EVIOCGABS(ABS_MT_SLOT), &slotInfo) == 0)
        m_currentSlot = qBound(0, slotInfo.value, MAX_SLOTS - 1);
    else
        m_currentSlot = 0;

    if (m_buttonStripHeld) {
        emitButton(m_buttonStripCode, false);
        m_buttonStripHeld = false;
    }
    m_buttonStripPending = false;
    m_gestureLiftedCount = 0;
    m_prevActiveCount = 0;
    m_peakActiveCount = 0;
    m_twoFingerCleanLiftCount = 0;

    qDebug() << "TouchInputHandler: SYN_DROPPED — slot state re-synced from kernel";
}

void TouchInputHandler::processSyncReport()
{
    if (m_touchpadRect.isEmpty())
        return;

    int activeCount = 0;
    int primarySlot = -1;
    int secondarySlot = -1;

    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (!m_slots[i].active) continue;
        ++activeCount;
        if (primarySlot < 0 || m_slots[i].id < m_slots[primarySlot].id)
            primarySlot = i;
    }
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (!m_slots[i].active || i == primarySlot) continue;
        if (secondarySlot < 0 || m_slots[i].id < m_slots[secondarySlot].id)
            secondarySlot = i;
    }

    const bool normalStart = (m_prevActiveCount == 0 && activeCount > 0);
    if (normalStart) {
        if (m_buttonStripHeld) {
            emitButton(m_buttonStripCode, false);
            m_buttonStripHeld = false;
        }
        m_buttonStripPending = false;
        m_gestureLiftedCount = 0;
        m_accumDX = 0.0;
        m_accumDY = 0.0;
        m_scrollAccum = 0.0;
        m_swipeAccumX = 0;
        m_swipeFired = false;
        m_pinchAccum = 0.0;
        m_peakActiveCount = 0;
        m_twoFingerCleanLiftCount = 0;
    }
    m_prevActiveCount = activeCount;
    if (activeCount > m_peakActiveCount)
        m_peakActiveCount = activeCount;

    if (primarySlot < 0)
        return;
    if (m_absXMax == m_absXMin || m_absYMax == m_absYMin)
        return;

    // Detect button strip for newly active slots.
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (!m_slots[i].active || !m_slots[i].firstMove) continue;
        if (m_buttonCount > 0) {
            const double spX = static_cast<double>(m_slots[i].x - m_absXMin) / (m_absXMax - m_absXMin) * m_screenW;
            const double spY = static_cast<double>(m_slots[i].y - m_absYMin) / (m_absYMax - m_absYMin) * m_screenH;
            const bool slotInTP = (spY >= m_touchpadRect.top() && spY <= m_touchpadRect.bottom());
            const double slotNX = spX / m_screenW;
            if (slotInTP && slotNX >= 0.333 && slotNX <= 0.667) {
                const double slotNY = (spY - m_touchpadRect.top()) / m_touchpadRect.height();
                if (slotNY >= BUTTON_STRIP_Y) {
                    m_slots[i].inButtonStrip = true;
                    const double slotTpNX = (slotNX - 0.333) / 0.334;
                    switch (m_buttonCount) {
                    case 1: m_slots[i].buttonIndex = 0; break;
                    case 2: m_slots[i].buttonIndex = (slotTpNX < 0.5) ? 0 : 1; break;
                    default: m_slots[i].buttonIndex = (slotTpNX < 0.333) ? 0 : (slotTpNX < 0.667) ? 1 : 2; break;
                    }
                }
            }
        }
        // Defer BTN press until a cursor finger is also active to prevent
        // accidental grazes from holding BTN_LEFT.
        if (m_slots[i].inButtonStrip && m_slots[i].buttonIndex >= 0 &&
            !m_buttonStripHeld && !m_buttonStripPending) {
            static const int BTN_CODES[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };
            const int code = BTN_CODES[std::min(m_slots[i].buttonIndex, 2)];
            m_buttonStripCode = (m_buttonCount == 2 && m_slots[i].buttonIndex == 1) ? BTN_RIGHT : code;
            m_buttonStripPending = true;
        }
    }

    // Prefer the non-button-strip slot as cursor primary.
    if (secondarySlot >= 0 &&
        m_slots[primarySlot].inButtonStrip && !m_slots[secondarySlot].inButtonStrip) {
        std::swap(primarySlot, secondarySlot);
    }

    Slot &s = m_slots[primarySlot];

    const double screenPixelX = static_cast<double>(s.x - m_absXMin)
                                 / (m_absXMax - m_absXMin) * m_screenW;
    const double screenPixelY = static_cast<double>(s.y - m_absYMin)
                                 / (m_absYMax - m_absYMin) * m_screenH;

    const bool primaryInTouchpad = (screenPixelY >= m_touchpadRect.top() &&
                                     screenPixelY <= m_touchpadRect.bottom());

    const double normX = screenPixelX / m_screenW;
    const bool primaryInCenter = (normX >= 0.333 && normX <= 0.667);

    if (s.inButtonStrip) {
        s.firstMove = false;
        for (int i = 0; i < MAX_SLOTS; ++i)
            if (m_slots[i].active && i != primarySlot)
                m_slots[i].firstMove = false;
        return;
    }

    // ── 1-finger pointer move (also handles button strip drag) ──
    const bool secondaryIsButton = (secondarySlot >= 0 && m_slots[secondarySlot].inButtonStrip);
    if (secondaryIsButton && m_buttonStripPending && !m_buttonStripHeld) {
        emitButton(m_buttonStripCode, true);
        m_buttonStripPending = false;
        m_buttonStripHeld = true;
    }
    if (m_gestureLiftedCount == 0 && primaryInTouchpad && primaryInCenter && !s.firstMove &&
        (activeCount == 1 || secondaryIsButton)) {
        const int rawDX = s.x - s.prevX;
        const int rawDY = s.y - s.prevY;
        if (std::abs(rawDX) + std::abs(rawDY) >= JITTER_THRESHOLD_RAW) {
            m_accumDX += rawDX * m_pointerSpeed;
            m_accumDY += rawDY * m_pointerSpeed;
            const int emitX = static_cast<int>(m_accumDX);
            const int emitY = static_cast<int>(m_accumDY);
            if (emitX != 0 || emitY != 0) {
                emitRelative(emitX, emitY);
                m_accumDX -= emitX;
                m_accumDY -= emitY;
            }
        }
    }

    // ── 2-finger scroll ──
    if (activeCount == 2 && m_gestureLiftedCount == 0 && secondarySlot >= 0 && !secondaryIsButton) {
        const Slot &s2 = m_slots[secondarySlot];
        if (!s.firstMove && !s2.firstMove) {
            const int rawDY = ((s.y - s.prevY) + (s2.y - s2.prevY)) / 2;
            m_scrollAccum -= rawDY * m_scrollSpeed * 6.0;
            const int hiRes = static_cast<int>(m_scrollAccum);
            if (hiRes != 0) {
                input_event ev{};
                ev.type = EV_REL;
                ev.code = REL_WHEEL_HI_RES;
                ev.value = hiRes;
                ::write(m_uinputFd, &ev, sizeof(ev));
                const int fullTicks = hiRes / 120;
                if (fullTicks != 0) {
                    ev.code = REL_WHEEL;
                    ev.value = fullTicks;
                    ::write(m_uinputFd, &ev, sizeof(ev));
                }
                emitSync();
                m_scrollAccum -= hiRes;
            }
        }
    }

    // ── 2-finger pinch zoom ──
    if (activeCount == 2 && m_pinchZoomEnabled && m_gestureLiftedCount == 0 &&
        secondarySlot >= 0 && !secondaryIsButton) {
        const Slot &s2 = m_slots[secondarySlot];
        if (!s.firstMove && !s2.firstMove) {
            const double currDist = std::hypot(double(s.x - s2.x), double(s.y - s2.y));
            const double prevDist = std::hypot(double(s.prevX - s2.prevX), double(s.prevY - s2.prevY));
            const double distDelta = std::abs(currDist - prevDist);
            // Centroid translation: non-zero when one finger is stationary (scroll).
            // True pinch has centroid ≈ stationary; one-finger-stationary scroll has
            // centroidSpeed = distDelta / 2 at worst.  Only accumulate when distance
            // change clearly dominates translation.
            const double cDX = ((s.x - s.prevX) + (s2.x - s2.prevX)) * 0.5;
            const double cDY = ((s.y - s.prevY) + (s2.y - s2.prevY)) * 0.5;
            const double centroidSpeed = std::hypot(cDX, cDY);
            if (distDelta > centroidSpeed * 2.0) {
                m_pinchAccum += (currDist - prevDist) * 0.1;
                const int hiRes = static_cast<int>(m_pinchAccum);
                if (hiRes != 0) {
                    emitCtrlScroll(hiRes);
                    m_pinchAccum -= hiRes;
                }
            }
        }
    }

    // ── 3-finger desktop switch ──
    if (activeCount == 3 && m_threeFingerSwipeEnabled && m_gestureLiftedCount == 0 && !m_swipeFired) {
        int totalDX = 0;
        int movers = 0;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (!m_slots[i].active || m_slots[i].firstMove) continue;
            totalDX += m_slots[i].x - m_slots[i].prevX;
            ++movers;
        }
        if (movers > 0) {
            m_swipeAccumX += totalDX / movers;
            const int threshold = (m_absXMax - m_absXMin) / 20;
            if (m_swipeAccumX > threshold) {
                switchDesktop(1);
                m_swipeFired = true;
            } else if (m_swipeAccumX < -threshold) {
                switchDesktop(-1);
                m_swipeFired = true;
            }
        }
    }


    for (int i = 0; i < MAX_SLOTS; ++i)
        if (m_slots[i].active)
            m_slots[i].firstMove = false;
}

void TouchInputHandler::handleFingerLift(int slotIdx)
{
    const Slot &s = m_slots[slotIdx];
    if (!s.active)
        return;

    if (s.inButtonStrip && (m_buttonStripHeld || m_buttonStripPending)) {
        if (m_buttonStripHeld) {
            emitButton(m_buttonStripCode, false);
            m_buttonStripHeld = false;
        } else {
            emitButton(m_buttonStripCode, true);
            emitButton(m_buttonStripCode, false);
        }
        m_buttonStripPending = false;
        return;
    }

    if (s.firstMove)
        return;

    if (m_twoFingerRightClickEnabled && m_peakActiveCount == 2 && !s.inButtonStrip &&
        std::abs(s.x - s.startX) + std::abs(s.y - s.startY) < TAP_THRESHOLD_RAW) {
        ++m_twoFingerCleanLiftCount;
        if (m_twoFingerCleanLiftCount == 2) {
            emitButton(BTN_RIGHT, true);
            emitButton(BTN_RIGHT, false);
        }
        return;
    }

    if (m_tapToClickEnabled && m_peakActiveCount == 1 && m_gestureLiftedCount == 0 &&
        !s.inButtonStrip &&
        std::abs(s.x - s.startX) + std::abs(s.y - s.startY) < TAP_THRESHOLD_RAW) {
        emitButton(BTN_LEFT, true);
        emitButton(BTN_LEFT, false);
        return;
    }

    ++m_gestureLiftedCount;
}

// ─── uinput emission ────────────────────────────────────────────────────────

void TouchInputHandler::emitRelative(int dx, int dy)
{
    input_event ev{};
    if (dx != 0) {
        ev.type = EV_REL; ev.code = REL_X; ev.value = dx;
        ::write(m_uinputFd, &ev, sizeof(ev));
    }
    if (dy != 0) {
        ev.type = EV_REL; ev.code = REL_Y; ev.value = dy;
        ::write(m_uinputFd, &ev, sizeof(ev));
    }
    emitSync();
}

void TouchInputHandler::emitButton(int btn, bool press)
{
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = static_cast<uint16_t>(btn);
    ev.value = press ? 1 : 0;
    ::write(m_uinputFd, &ev, sizeof(ev));
    emitSync();
}

void TouchInputHandler::emitSync()
{
    input_event ev{};
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ::write(m_uinputFd, &ev, sizeof(ev));
}

void TouchInputHandler::emitCtrlScroll(int hiResUnits)
{
    input_event ev{};
    ev.type = EV_KEY; ev.code = KEY_LEFTCTRL; ev.value = 1;
    ::write(m_uinputFd, &ev, sizeof(ev));
    emitSync();

    ev.type = EV_REL; ev.code = REL_WHEEL_HI_RES; ev.value = hiResUnits;
    ::write(m_uinputFd, &ev, sizeof(ev));
    const int ticks = hiResUnits / 120;
    if (ticks != 0) {
        ev.type = EV_REL; ev.code = REL_WHEEL; ev.value = ticks;
        ::write(m_uinputFd, &ev, sizeof(ev));
    }
    emitSync();

    ev.type = EV_KEY; ev.code = KEY_LEFTCTRL; ev.value = 0;
    ::write(m_uinputFd, &ev, sizeof(ev));
    emitSync();
}

void TouchInputHandler::switchDesktop(int direction)
{
    const QString method = direction > 0
        ? QStringLiteral("nextDesktop")
        : QStringLiteral("previousDesktop");
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        method);
    QDBusConnection::sessionBus().send(msg);
}


} // namespace PlasmaTP
