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
// 2-finger gesture classification thresholds.
// SCROLL: hi-res units (cDY * scrollSpeed * 6) before committing.  Must be > 3 ×
//   DRAG_MOVER_RAW so anchor-drag always classifies before scroll does.
// DRAG: anchor finger must not have moved more than DRAG_ANCHOR_RAW raw units from
//   its start position, while the mover has already travelled DRAG_MOVER_RAW.
// Scroll/pinch/rotate: both fingers must have moved at least this far from their
// re-arm start positions before any symmetric gesture can classify.  Ensures all
// three gestures fire only when both fingers are genuinely moving.
static constexpr int    GESTURE_BOTH_MOVING_RAW = 30;
// Drag (anchor + mover): mover must exceed DRAG_MOVER and the anchor must have
// traveled less than 1/3 of the mover's cumulative distance.  The ratio check
// is scale-independent and tolerates natural anchor drift without a fixed floor.
static constexpr int    GESTURE_DRAG_MOVER_RAW  = 85;
static constexpr double GESTURE_CLASSIFY_SCROLL = 320.0;  // > 3 × 80 = 240
static constexpr double GESTURE_CLASSIFY_PINCH  = 2.5;
static constexpr double GESTURE_CLASSIFY_ROTATE = 8.0;

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
    uinputSetBit(m_uinputFd, UI_SET_RELBIT, REL_DIAL);

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

    {
        const bool wasHighlighted = m_buttonStripHeld || m_buttonStripPending;
        if (m_buttonStripHeld) {
            emitButton(m_buttonStripCode, false);
            m_buttonStripHeld = false;
        }
        m_buttonStripPending = false;
        if (wasHighlighted) Q_EMIT buttonHighlightChanged(-1);
    }
    m_gestureLiftedCount = 0;
    m_prevActiveCount = 0;
    m_peakActiveCount = 0;
    m_twoFingerCleanLiftCount = 0;
    m_pendingCursorDX = 0;
    m_pendingCursorDY = 0;

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
        const bool wasHighlighted = m_buttonStripHeld || m_buttonStripPending;
        if (m_buttonStripHeld) {
            emitButton(m_buttonStripCode, false);
            m_buttonStripHeld = false;
        }
        m_buttonStripPending = false;
        if (wasHighlighted) Q_EMIT buttonHighlightChanged(-1);
        m_gestureLiftedCount = 0;
        m_accumDX = 0.0;
        m_accumDY = 0.0;
        m_pendingCursorDX = 0;
        m_pendingCursorDY = 0;
        m_scrollAccum = 0.0;
        m_swipeAccumX = 0;
        m_swipeFired = false;
        m_pinchAccum = 0.0;
        m_rotateAccum = 0.0;
        m_twoFingerGesture = TwoFingerGesture::None;
        if (m_dragButtonHeld) {
            emitButton(BTN_LEFT, false);
            m_dragButtonHeld = false;
        }
        m_peakActiveCount = 0;
        m_twoFingerCleanLiftCount = 0;
    }
    // Re-arm when a second finger joins a single-finger contact (anchor + new scroll/drag).
    // Resets gesture classification and start positions so thresholds measure from here.
    if (m_prevActiveCount == 1 && activeCount == 2) {
        m_gestureLiftedCount = 0;
        m_twoFingerGesture = TwoFingerGesture::None;
        m_twoFingerCleanLiftCount = 0;
        m_scrollAccum = 0.0;
        m_pinchAccum  = 0.0;
        m_rotateAccum = 0.0;
        m_pendingCursorDX = 0;
        m_pendingCursorDY = 0;
        if (m_dragButtonHeld) {
            emitButton(BTN_LEFT, false);
            m_dragButtonHeld = false;
        }
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (m_slots[i].active) {
                m_slots[i].startX = m_slots[i].x;
                m_slots[i].startY = m_slots[i].y;
            }
        }
    }
    // Handle a 1→3 count jump: two fingers arriving in the same sync report while
    // one is already down skips both the 1→2 and 2→3 re-arms, leaving
    // m_gestureLiftedCount and m_swipeFired stuck from the previous gesture and
    // permanently blocking cursor movement and new swipes.
    if (m_prevActiveCount == 1 && activeCount == 3) {
        m_gestureLiftedCount = 0;
        m_swipeFired = false;
        m_swipeAccumX = 0;
        m_twoFingerGesture = TwoFingerGesture::None;
        m_twoFingerCleanLiftCount = 0;
        m_scrollAccum = 0.0;
        m_pinchAccum  = 0.0;
        m_rotateAccum = 0.0;
        m_pendingCursorDX = 0;
        m_pendingCursorDY = 0;
        if (m_dragButtonHeld) {
            emitButton(BTN_LEFT, false);
            m_dragButtonHeld = false;
        }
        for (int i = 0; i < MAX_SLOTS; ++i) {
            if (m_slots[i].active) {
                m_slots[i].startX = m_slots[i].x;
                m_slots[i].startY = m_slots[i].y;
            }
        }
    }
    // If a drag-select was active when the 3rd finger arrives, release BTN_LEFT
    // so it doesn't bleed into the desktop-switch swipe.
    if (m_prevActiveCount == 2 && activeCount == 3 && m_dragButtonHeld) {
        emitButton(BTN_LEFT, false);
        m_dragButtonHeld = false;
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
            Q_EMIT buttonHighlightChanged(m_slots[i].buttonIndex);
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
        // Flush last frame's buffered move — finger is confirmed still active.
        if (m_pendingCursorDX != 0 || m_pendingCursorDY != 0) {
            emitRelative(m_pendingCursorDX, m_pendingCursorDY);
            m_pendingCursorDX = 0;
            m_pendingCursorDY = 0;
        }
        const int rawDX = s.x - s.prevX;
        const int rawDY = s.y - s.prevY;
        if (std::abs(rawDX) + std::abs(rawDY) >= JITTER_THRESHOLD_RAW) {
            m_accumDX += rawDX * m_pointerSpeed;
            m_accumDY += rawDY * m_pointerSpeed;
            const int emitX = static_cast<int>(m_accumDX);
            const int emitY = static_cast<int>(m_accumDY);
            if (emitX != 0 || emitY != 0) {
                m_pendingCursorDX = emitX;
                m_pendingCursorDY = emitY;
                m_accumDX -= emitX;
                m_accumDY -= emitY;
            }
        }
    }

    // ── 2-finger gestures (mutually exclusive) ──
    // All signals accumulate every frame while unclassified.  The first to cross
    // its threshold commits the gesture type for the rest of the contact.
    // Drag (anchor + mover) is checked before scroll because in an anchor-drag the
    // centroid still moves, which would otherwise classify as scroll first.
    if (activeCount == 2 && m_gestureLiftedCount == 0 && secondarySlot >= 0 && !secondaryIsButton) {
        const Slot &s2 = m_slots[secondarySlot];
        if (!s.firstMove && !s2.firstMove) {
            const double cDX = ((s.x - s.prevX) + (s2.x - s2.prevX)) * 0.5;
            const double cDY = ((s.y - s.prevY) + (s2.y - s2.prevY)) * 0.5;
            const double currDist = std::hypot(double(s.x - s2.x), double(s.y - s2.y));
            const double prevDist = std::hypot(double(s.prevX - s2.prevX), double(s.prevY - s2.prevY));
            const double distDelta = currDist - prevDist;
            const double prevAngle = std::atan2(double(s.prevY - s2.prevY), double(s.prevX - s2.prevX));
            const double currAngle = std::atan2(double(s.y - s2.y), double(s.x - s2.x));
            double angleDelta = currAngle - prevAngle;
            if (angleDelta >  M_PI) angleDelta -= 2.0 * M_PI;
            if (angleDelta < -M_PI) angleDelta += 2.0 * M_PI;

            const double centroidSpeed = std::hypot(cDX, cDY);
            // Scroll: both fingers must be moving in the same Y direction this frame
            // (dy product >= 0) AND centroid translation must dominate distance change.
            // The direction gate blocks frames where one finger reverses while the other
            // continues, which the centroid filter alone misses (net centroid can still
            // be nonzero while one finger goes opposite).
            const int dy1Frame = s.y - s.prevY;
            const int dy2Frame = s2.y - s2.prevY;
            if (dy1Frame * dy2Frame >= 0 && std::abs(distDelta) <= centroidSpeed)
                m_scrollAccum -= cDY * m_scrollSpeed * 6.0;
            // Pinch: accumulate only when distance change dominates centroid translation.
            // During scroll the centroid moves fast while distDelta stays small.
            if (std::abs(distDelta) > centroidSpeed * 2.0)
                m_pinchAccum += distDelta * 0.1;
            if (std::abs(angleDelta) > 0.005)
                m_rotateAccum += angleDelta * (180.0 / M_PI);

            if (m_twoFingerGesture == TwoFingerGesture::None) {
                const int sDist  = std::abs(s.x  - s.startX)  + std::abs(s.y  - s.startY);
                const int s2Dist = std::abs(s2.x - s2.startX) + std::abs(s2.y - s2.startY);
                const int anchorDist = std::min(sDist, s2Dist);
                const int moverDist  = std::max(sDist, s2Dist);
                if (moverDist >= GESTURE_DRAG_MOVER_RAW && anchorDist * 4 < moverDist) {
                    // One finger barely moved relative to the other → drag-select.
                    // Also require the anchor to be stationary THIS frame: a recently-
                    // placed scroll finger that hasn't accumulated cumulative travel yet
                    // looks like a stationary anchor by distance alone, but its per-frame
                    // velocity betrays that it is actively moving.
                    const Slot &anchorSlot = (sDist <= s2Dist) ? s : s2;
                    const int anchorFrameSpeed = std::abs(anchorSlot.x - anchorSlot.prevX)
                                               + std::abs(anchorSlot.y - anchorSlot.prevY);
                    if (anchorFrameSpeed < JITTER_THRESHOLD_RAW) {
                        m_twoFingerGesture = TwoFingerGesture::Drag;
                        emitButton(BTN_LEFT, true);
                        m_dragButtonHeld = true;
                    }
                } else if (anchorDist >= GESTURE_BOTH_MOVING_RAW) {
                    // Both fingers genuinely moving → symmetric gestures only.
                    // Scroll requires both fingers moving in the same direction:
                    // dot product of cumulative displacement vectors must be positive.
                    const int dx1 = s.x  - s.startX,  dy1 = s.y  - s.startY;
                    const int dx2 = s2.x - s2.startX, dy2 = s2.y - s2.startY;
                    const bool sameDirection = (dx1 * dx2 + dy1 * dy2 > 0);
                    if (sameDirection && std::abs(m_scrollAccum) >= GESTURE_CLASSIFY_SCROLL)
                        m_twoFingerGesture = TwoFingerGesture::Scroll;
                    else if (std::abs(m_pinchAccum) >= GESTURE_CLASSIFY_PINCH)
                        m_twoFingerGesture = TwoFingerGesture::Pinch;
                    else if (m_rotateEnabled && std::abs(m_rotateAccum) >= GESTURE_CLASSIFY_ROTATE)
                        m_twoFingerGesture = TwoFingerGesture::Rotate;
                }
            }

            switch (m_twoFingerGesture) {
            case TwoFingerGesture::Scroll: {
                const int raw = static_cast<int>(m_scrollAccum);
                if (raw != 0) {
                    const int hiRes = m_invertScroll ? -raw : raw;
                    input_event ev{};
                    ev.type = EV_REL; ev.code = REL_WHEEL_HI_RES; ev.value = hiRes;
                    ::write(m_uinputFd, &ev, sizeof(ev));
                    const int fullTicks = hiRes / 120;
                    if (fullTicks != 0) {
                        ev.code = REL_WHEEL; ev.value = fullTicks;
                        ::write(m_uinputFd, &ev, sizeof(ev));
                    }
                    emitSync();
                    m_scrollAccum -= raw;
                }
                break;
            }
            case TwoFingerGesture::Drag: {
                // Cursor follows the more-mobile finger (re-evaluated each frame).
                const int sDist  = std::abs(s.x  - s.startX)  + std::abs(s.y  - s.startY);
                const int s2Dist = std::abs(s2.x - s2.startX) + std::abs(s2.y - s2.startY);
                const Slot &mover = (sDist >= s2Dist) ? s : s2;
                const int rawDX = mover.x - mover.prevX;
                const int rawDY = mover.y - mover.prevY;
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
                break;
            }
            case TwoFingerGesture::Pinch:
                if (m_pinchZoomEnabled) {
                    const int hiRes = static_cast<int>(m_pinchAccum);
                    if (hiRes != 0) { emitCtrlScroll(hiRes); m_pinchAccum -= hiRes; }
                }
                break;
            case TwoFingerGesture::Rotate:
                if (m_rotateEnabled) {
                    const int ticks = static_cast<int>(m_rotateAccum);
                    if (ticks != 0) { emitDial(ticks); m_rotateAccum -= ticks; }
                }
                break;
            default:
                break;
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

    if (errno == ENODEV || errno == EIO) {
        qWarning() << "TouchInputHandler: device error" << errno << "- signalling exit";
        m_notifier->setEnabled(false);
        Q_EMIT deviceError();
        return;
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

    m_pendingCursorDX = 0;
    m_pendingCursorDY = 0;

    if (s.inButtonStrip && (m_buttonStripHeld || m_buttonStripPending)) {
        if (m_buttonStripHeld) {
            emitButton(m_buttonStripCode, false);
            m_buttonStripHeld = false;
        } else {
            emitButton(m_buttonStripCode, true);
            emitButton(m_buttonStripCode, false);
        }
        m_buttonStripPending = false;
        Q_EMIT buttonHighlightChanged(-1);
        return;
    }

    if (s.firstMove)
        return;

    // Release drag button when any finger lifts during a drag.
    if (m_dragButtonHeld) {
        emitButton(BTN_LEFT, false);
        m_dragButtonHeld = false;
        ++m_gestureLiftedCount;
        return;
    }

    // Right-click: both fingers tapped without committing to any gesture.
    if (m_twoFingerRightClickEnabled && m_peakActiveCount == 2 && !s.inButtonStrip &&
        m_twoFingerGesture == TwoFingerGesture::None &&
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

void TouchInputHandler::emitDial(int ticks)
{
    input_event ev{};
    ev.type = EV_REL;
    ev.code = REL_DIAL;
    ev.value = ticks;
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
