#include "TouchInputHandler.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QDebug>

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

static constexpr int TAP_THRESHOLD = 400;   // raw units
static constexpr int TAP_TIME_MS = 180;     // ms — must be a quick contact to register as tap
static constexpr int DOUBLE_TAP_MS = 300;   // ms between taps
static constexpr double BUTTON_STRIP_Y = 0.88;

static void uinputSetBit(int fd, int request, int bit)
{
    ::ioctl(fd, request, bit);
}

static void kwinSwitchDesktop(bool next)
{
    const QString method = next ? QStringLiteral("nextDesktop")
                                : QStringLiteral("previousDesktop");
    auto msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/KWin"),
        QStringLiteral("org.kde.KWin"),
        method);
    QDBusConnection::sessionBus().send(msg);
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
    if (!openTouchscreen() || !openUinput() || !openUinputKeyboard()) {
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
    if (m_uinputKbdFd >= 0) {
        ::ioctl(m_uinputKbdFd, UI_DEV_DESTROY);
        ::close(m_uinputKbdFd);
        m_uinputKbdFd = -1;
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

    // Pointer-only capabilities — NO keyboard keys here.
    // Advertising keyboard keycodes on this device causes libinput to classify
    // it as a keyboard, which triggers KDE's "hide cursor while typing" on every
    // gesture keystroke. Keyboard events go to m_uinputKbdFd instead.
    uinputSetBit(m_uinputFd, UI_SET_EVBIT, EV_KEY);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_LEFT);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_MIDDLE);
    uinputSetBit(m_uinputFd, UI_SET_KEYBIT, BTN_RIGHT);
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

bool TouchInputHandler::openUinputKeyboard()
{
    m_uinputKbdFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_uinputKbdFd < 0) {
        qWarning() << "TouchInputHandler: cannot open /dev/uinput (kbd):" << strerror(errno);
        return false;
    }

    uinputSetBit(m_uinputKbdFd, UI_SET_EVBIT, EV_KEY);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_LEFTCTRL);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_LEFT);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_RIGHT);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_BACK);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_FORWARD);
    uinputSetBit(m_uinputKbdFd, UI_SET_KEYBIT, KEY_ZOOM);

    uinput_setup kbdsetup{};
    kbdsetup.id.bustype = BUS_VIRTUAL;
    kbdsetup.id.vendor = 0x1;
    kbdsetup.id.product = 0x2;
    ::strncpy(kbdsetup.name, "Plasma Touchpad Gestures", UINPUT_MAX_NAME_SIZE);

    if (::ioctl(m_uinputKbdFd, UI_DEV_SETUP, &kbdsetup) < 0 ||
        ::ioctl(m_uinputKbdFd, UI_DEV_CREATE) < 0) {
        qWarning() << "TouchInputHandler: uinput keyboard device creation failed";
        return false;
    }

    qDebug() << "TouchInputHandler: uinput virtual keyboard created";
    return true;
}

// ─── event processing ───────────────────────────────────────────────────────

void TouchInputHandler::touchReadyRead(int fd)
{
    input_event ev{};
    while (::read(fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        // After SYN_DROPPED: discard all events until the SYN_REPORT that closes
        // the partial frame, then re-sync slot state from the kernel via ioctl.
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
                qDebug() << "TIH: tid slot" << m_currentSlot << "id" << ev.value;
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
                m_slots[m_currentSlot].prevX = m_slots[m_currentSlot].x;
                m_slots[m_currentSlot].x = ev.value;
                break;

            case ABS_MT_POSITION_Y:
                m_slots[m_currentSlot].prevY = m_slots[m_currentSlot].y;
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
    // Re-query every slot's tracking ID from the kernel so we know exactly
    // which contacts are still down.  Per the evdev spec, after SYN_DROPPED
    // we must use EVIOCG* ioctls rather than trust the event stream.
    struct {
        __u32 code;
        __s32 values[MAX_SLOTS];
    } req;

    req.code = ABS_MT_TRACKING_ID;
    if (::ioctl(m_touchFd, EVIOCGMTSLOTS(sizeof(req)), &req) < 0) {
        // ioctl failed — conservatively clear all slots
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
                m_slots[i].firstMove = true;  // re-baseline to avoid coordinate jump
            }
        }
        // Refresh stored coordinates so resumed fingers don't produce a phantom delta
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

    // Resync current-slot index
    input_absinfo slotInfo{};
    if (::ioctl(m_touchFd, EVIOCGABS(ABS_MT_SLOT), &slotInfo) == 0)
        m_currentSlot = qBound(0, slotInfo.value, MAX_SLOTS - 1);
    else
        m_currentSlot = 0;

    // Abort the current gesture session — we cannot reconstruct what was lost
    m_peakActiveCount = 0;
    m_gestureLiftedCount = 0;
    m_gestureAnyFarMove = false;
    m_multiGesture = MultiGesture::Unknown;
    m_pinchLastDist = 0.0;
    m_pinchAccum = 0.0;
    m_hswipeAccum = 0.0;
    m_fourSwipeAccum = 0.0;
    m_prevActiveCount = 0;  // force a fresh session start on the next SYN_REPORT

    qDebug() << "TouchInputHandler: SYN_DROPPED — slot state re-synced from kernel";
}

void TouchInputHandler::processSyncReport()
{
    ++m_syncCount;

    if (m_touchpadRect.isEmpty())
        return;

    int activeCount = 0;
    int primarySlot = -1;
    int secondarySlot = -1;

    // Two-pass: find the two active slots with the lowest tracking IDs.
    // Single-pass "replace primary, discard old primary" drops the old primary
    // when IDs arrive out of slot order, leaving secondarySlot == -1 even with
    // two active fingers, which silences both the 1-finger and 2-finger paths.
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

    // New gesture session starts when first finger touches after all have lifted.
    const bool normalStart = (m_prevActiveCount == 0 && activeCount > 0);
    if (normalStart) {
        qDebug() << "TIH: session reset"
                 << "prevActive" << m_prevActiveCount
                 << "active" << activeCount
                 << "peak" << m_peakActiveCount
                 << "lifted" << m_gestureLiftedCount
                 << "anyFarMove" << m_gestureAnyFarMove;
        m_peakActiveCount = 0;
        m_gestureLiftedCount = 0;
        m_gestureAnyFarMove = false;
        m_multiGesture = MultiGesture::Unknown;
        m_pinchLastDist = 0.0;
        m_pinchAccum = 0.0;
        m_hswipeAccum = 0.0;
        m_fourSwipeAccum = 0.0;
        m_gestureSessionTimer.start();
    }
    m_peakActiveCount = std::max(m_peakActiveCount, activeCount);
    m_prevActiveCount = activeCount;
    qDebug() << "TIH: sync active" << activeCount << "peak" << m_peakActiveCount << "lifted" << m_gestureLiftedCount << "anyFarMove" << m_gestureAnyFarMove;

    if (primarySlot < 0)
        return;
    if (m_absXMax == m_absXMin || m_absYMax == m_absYMin)
        return;

    Slot &s = m_slots[primarySlot];

    // Initialize startX/startY for every active slot on its first frame.
    // Must happen before any dead-zone returns so that handleFingerLift can
    // compute thisFarMove correctly for all slots in multi-finger gestures.
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (m_slots[i].active && m_slots[i].firstMove) {
            m_slots[i].startX = m_slots[i].x;
            m_slots[i].startY = m_slots[i].y;
        }
    }

    const double screenPixelX = static_cast<double>(s.x - m_absXMin)
                                 / (m_absXMax - m_absXMin) * m_screenW;
    const double screenPixelY = static_cast<double>(s.y - m_absYMin)
                                 / (m_absYMax - m_absYMin) * m_screenH;

    const bool primaryInTouchpad = (screenPixelY >= m_touchpadRect.top() &&
                                     screenPixelY <= m_touchpadRect.bottom());

    const double normX = screenPixelX / m_screenW;
    const double normY = primaryInTouchpad
                         ? (screenPixelY - m_touchpadRect.top()) / m_touchpadRect.height()
                         : 0.0;
    const bool primaryInCenter = (normX >= 0.333 && normX <= 0.667);
    const double tpNormX = (normX - 0.333) / 0.334;

    // Button strip and tap zone detection — only when primary is in the active area.
    if (s.firstMove && primaryInTouchpad && primaryInCenter) {
        s.inButtonStrip = (normY >= BUTTON_STRIP_Y) && (m_buttonCount > 0);
        if (s.inButtonStrip) {
            switch (m_buttonCount) {
            case 1:
                s.buttonIndex = 0;
                break;
            case 2:
                s.buttonIndex = (tpNormX < 0.5) ? 0 : 1;
                break;
            default:
                s.buttonIndex = (tpNormX < 0.333) ? 0 : (tpNormX < 0.667) ? 1 : 2;
                break;
            }
        }
    }

    if (s.inButtonStrip) {
        s.firstMove = false;
        // Reset all other active slots too so firstMove accounting stays consistent.
        for (int i = 0; i < MAX_SLOTS; ++i)
            if (m_slots[i].active && i != primarySlot)
                m_slots[i].firstMove = false;
        return;
    }

    // ── 1-finger: pointer move ───────────────────────────────────────────────
    // Restricted to center zone and touchpad Y bounds (palm rejection).
    // m_gestureLiftedCount == 0 blocks the last remaining finger of a multi-
    // finger swipe from spuriously moving the cursor before all fingers lift.
    if (activeCount == 1 && m_gestureLiftedCount == 0 && primaryInTouchpad && primaryInCenter && !s.firstMove) {
        const int rawDX = s.x - s.prevX;
        const int rawDY = s.y - s.prevY;
        if (rawDX != 0 || rawDY != 0)
            emitRelative(static_cast<int>(rawDX * m_pointerSpeed),
                         static_cast<int>(rawDY * m_pointerSpeed));
    }

    // ── 2-finger gestures ───────────────────────────────────────────────────
    // m_gestureLiftedCount == 0 blocks the 2-finger tail of a 3+-finger swipe
    // from mis-firing scroll or page-swipe events while fingers peel off.
    if (activeCount == 2 && m_gestureLiftedCount == 0 && secondarySlot >= 0) {
        const Slot &s2 = m_slots[secondarySlot];

        // Pixel-space inter-finger distance for pinch detection
        const double range = static_cast<double>(m_absXMax - m_absXMin);
        const double px1 = static_cast<double>(s.x - m_absXMin) / range * m_screenW;
        const double py1 = static_cast<double>(s.y - m_absYMin) / (m_absYMax - m_absYMin) * m_screenH;
        const double px2 = static_cast<double>(s2.x - m_absXMin) / range * m_screenW;
        const double py2 = static_cast<double>(s2.y - m_absYMin) / (m_absYMax - m_absYMin) * m_screenH;
        const double dist = std::sqrt((px2-px1)*(px2-px1) + (py2-py1)*(py2-py1));

        // Initialise baseline distance whenever either finger is new.
        // Checking only s.firstMove misses the case where the primary was
        // already active when the second finger joined: m_pinchLastDist stays 0,
        // and the classifier sees a ~200 px spurious delta and mis-fires Pinch.
        if (s.firstMove || s2.firstMove)
            m_pinchLastDist = dist;

        // Pixel-space conversion factors — thresholds must be resolution-independent.
        // Raw axis units vary widely across touchscreens (0-4095, 0-32767, etc.);
        // using raw units for thresholds would make gestures wildly over/under-sensitive.
        const double rawToPixX = static_cast<double>(m_screenW) / (m_absXMax - m_absXMin);
        const double rawToPixY = static_cast<double>(m_screenH) / (m_absYMax - m_absYMin);

        // Commit to gesture type on first real movement
        if (m_multiGesture == MultiGesture::Unknown && !s.firstMove && !s2.firstMove) {
            const double cdx_px = std::abs(((s.x - s.prevX) + (s2.x - s2.prevX)) * rawToPixX);
            const double cdy_px = std::abs(((s.y - s.prevY) + (s2.y - s2.prevY)) * rawToPixY);
            const double ddist = std::abs(dist - m_pinchLastDist);
            if (ddist > 8.0 && ddist > std::max(cdx_px, cdy_px) * 0.5)
                m_multiGesture = MultiGesture::Pinch;
            else if (cdy_px >= cdx_px && cdy_px > 1.0)
                m_multiGesture = MultiGesture::Scroll;
            else if (cdx_px > cdy_px && cdx_px > 1.0)
                m_multiGesture = MultiGesture::HorizSwipe;
        }

        if (!s.firstMove && !s2.firstMove) {
            switch (m_multiGesture) {
            case MultiGesture::Scroll:
                if (m_gestureScroll) {
                    const int rawDY = ((s.y - s.prevY) + (s2.y - s2.prevY)) / 2;
                    if (rawDY != 0) {
                        const int ticks = static_cast<int>(rawDY * m_scrollSpeed / 20.0);
                        if (ticks != 0)
                            emitWheel(-ticks);
                    }
                }
                break;

            case MultiGesture::Pinch:
                if (m_gesturePinchZoom && m_pinchLastDist > 0.0) {
                    m_pinchAccum += dist - m_pinchLastDist;
                    const int ticks = static_cast<int>(m_pinchAccum / 25.0);
                    if (ticks != 0) {
                        // Hold Ctrl on the kbd device, scroll on the pointer device,
                        // then release Ctrl. Both devices share the same seat so
                        // apps receive Ctrl+Wheel as expected.
                        emitKeyDown(KEY_LEFTCTRL);
                        {
                            input_event syn{}; syn.type = EV_SYN; syn.code = SYN_REPORT;
                            ::write(m_uinputKbdFd, &syn, sizeof(syn));
                        }
                        emitWheel(ticks);
                        emitKeyUp(KEY_LEFTCTRL);
                        {
                            input_event syn{}; syn.type = EV_SYN; syn.code = SYN_REPORT;
                            ::write(m_uinputKbdFd, &syn, sizeof(syn));
                        }
                        m_pinchAccum -= ticks * 25.0;
                    }
                }
                break;

            case MultiGesture::HorizSwipe:
                m_hswipeAccum += static_cast<double>((s.x - s.prevX) + (s2.x - s2.prevX)) / 2.0;
                break;

            default:
                break;
            }
        }

        m_pinchLastDist = dist;
    }

    // ── 3+ finger: switch desktops ──────────────────────────────────────────
    if (activeCount >= 3 && m_gestureSwipeDesktops) {
        int totalDX = 0, movedCount = 0;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (!m_slots[i].active || m_slots[i].firstMove) continue;
            totalDX += m_slots[i].x - m_slots[i].prevX;
            ++movedCount;
        }
        if (movedCount > 0) {
            m_fourSwipeAccum += static_cast<double>(totalDX) / movedCount;
            const double pixPerSwipe = (m_absXMax - m_absXMin) * 0.12;
            while (m_fourSwipeAccum > pixPerSwipe) {
                kwinSwitchDesktop(true);   // swipe right → next desktop
                m_fourSwipeAccum -= pixPerSwipe;
            }
            while (m_fourSwipeAccum < -pixPerSwipe) {
                kwinSwitchDesktop(false);  // swipe left → previous desktop
                m_fourSwipeAccum += pixPerSwipe;
            }
        }
    }

    // Reset firstMove on all active slots once we've consumed this frame
    for (int i = 0; i < MAX_SLOTS; ++i)
        if (m_slots[i].active)
            m_slots[i].firstMove = false;
}

void TouchInputHandler::handleFingerLift(int slotIdx)
{
    const Slot &s = m_slots[slotIdx];
    if (!s.active)
        return;

    // If firstMove is still true, this slot never went through a processSyncReport
    // cycle — the kernel cycled its tracking ID without us seeing any settled frame.
    // This happens on hardware that continuously reassigns IDs for held contacts.
    // Counting these as real lifts would reset the session in a tight loop.
    qDebug() << "TIH: fingerLift slot" << slotIdx << "firstMove" << s.firstMove
             << "peak" << m_peakActiveCount << "lifted" << m_gestureLiftedCount;
    if (s.firstMove)
        return;

    const bool thisFarMove = std::abs(s.x - s.startX) >= TAP_THRESHOLD ||
                              std::abs(s.y - s.startY) >= TAP_THRESHOLD;
    if (thisFarMove)
        m_gestureAnyFarMove = true;
    ++m_gestureLiftedCount;
    qDebug() << "TIH: lift counted peak" << m_peakActiveCount << "lifted" << m_gestureLiftedCount << "farMove" << m_gestureAnyFarMove;

    // Two-finger tap and horizontal swipe completion
    if (m_peakActiveCount == 2 && m_gestureLiftedCount == 2) {
        const bool shortGesture = m_gestureSessionTimer.elapsed() < TAP_TIME_MS;
        if (!m_gestureAnyFarMove && shortGesture) {
            // Two-finger tap
            if (m_awaitingDoubleTap && m_lastTwoTapTimer.elapsed() < DOUBLE_TAP_MS) {
                if (m_gestureSmartZoom)
                    emitKey(KEY_ZOOM);
                m_awaitingDoubleTap = false;
            } else {
                if (m_gestureRightClick) {
                    emitButton(BTN_RIGHT, true);
                    emitButton(BTN_RIGHT, false);
                }
                m_awaitingDoubleTap = m_gestureSmartZoom;
                m_lastTwoTapTimer.start();
            }
        } else if (m_multiGesture == MultiGesture::HorizSwipe && m_gestureSwipePages) {
            const double threshold = (m_absXMax - m_absXMin) * 0.05;
            if (m_hswipeAccum > threshold)
                emitKey(KEY_FORWARD);
            else if (m_hswipeAccum < -threshold)
                emitKey(KEY_BACK);
        }
        return;
    }

    // Three-finger tap → middle click
    if (m_peakActiveCount == 3 && m_gestureLiftedCount == 3) {
        const bool shortGesture = m_gestureSessionTimer.elapsed() < TAP_TIME_MS;
        if (!m_gestureAnyFarMove && shortGesture) {
            emitButton(BTN_MIDDLE, true);
            emitButton(BTN_MIDDLE, false);
        }
        return;
    }

    // Single-finger tap
    if (m_peakActiveCount == 1 && !thisFarMove) {
        if (s.inButtonStrip && s.buttonIndex >= 0) {
            static const int BTN_CODES[] = { BTN_LEFT, BTN_MIDDLE, BTN_RIGHT };
            const int code = BTN_CODES[std::min(s.buttonIndex, 2)];
            const int btn = (m_buttonCount == 2 && s.buttonIndex == 1) ? BTN_RIGHT : code;
            emitButton(btn, true);
            emitButton(btn, false);
        } else if (!s.inButtonStrip && m_tapToClick) {
            emitButton(BTN_LEFT, true);
            emitButton(BTN_LEFT, false);
        }
    }
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

void TouchInputHandler::emitWheel(int ticks)
{
    input_event ev{};
    ev.type = EV_REL;
    ev.code = REL_WHEEL_HI_RES;
    ev.value = ticks * 120;
    ::write(m_uinputFd, &ev, sizeof(ev));
    ev.code = REL_WHEEL;
    ev.value = ticks;
    ::write(m_uinputFd, &ev, sizeof(ev));
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

void TouchInputHandler::emitKeyDown(int key)
{
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = static_cast<uint16_t>(key);
    ev.value = 1;
    ::write(m_uinputKbdFd, &ev, sizeof(ev));
}

void TouchInputHandler::emitKeyUp(int key)
{
    input_event ev{};
    ev.type = EV_KEY;
    ev.code = static_cast<uint16_t>(key);
    ev.value = 0;
    ::write(m_uinputKbdFd, &ev, sizeof(ev));
}

void TouchInputHandler::emitKey(int key)
{
    emitKeyDown(key);
    emitKeyUp(key);
    // Flush the keyboard device
    input_event syn{};
    syn.type = EV_SYN; syn.code = SYN_REPORT;
    ::write(m_uinputKbdFd, &syn, sizeof(syn));
}

void TouchInputHandler::emitKeyCombo(int mod, int key)
{
    emitKeyDown(mod);
    emitKeyDown(key);
    emitKeyUp(key);
    emitKeyUp(mod);
    input_event syn{};
    syn.type = EV_SYN; syn.code = SYN_REPORT;
    ::write(m_uinputKbdFd, &syn, sizeof(syn));
}

void TouchInputHandler::emitKeyCombo(int mod1, int mod2, int key)
{
    emitKeyDown(mod1);
    emitKeyDown(mod2);
    emitKeyDown(key);
    emitKeyUp(key);
    emitKeyUp(mod2);
    emitKeyUp(mod1);
    input_event syn{};
    syn.type = EV_SYN; syn.code = SYN_REPORT;
    ::write(m_uinputKbdFd, &syn, sizeof(syn));
}

void TouchInputHandler::emitSync()
{
    input_event ev{};
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ::write(m_uinputFd, &ev, sizeof(ev));
}

} // namespace PlasmaTP
