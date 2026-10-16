#pragma once

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSocketNotifier>
#include <linux/input.h>

namespace PlasmaTP {

class TouchInputHandler : public QObject
{
    Q_OBJECT

public:
    explicit TouchInputHandler(QObject *parent = nullptr);
    ~TouchInputHandler() override;

    void setTouchpadRect(const QRectF &rect);
    void setScreenSize(int w, int h) { m_screenW = w; m_screenH = h; }

    void setPointerSpeed(double speed) { m_pointerSpeed = speed; }
    void setPointerAcceleration(double amount) { m_pointerAccel = qBound(0.0, amount, 1.0); }
    void setScrollSpeed(double speed) { m_scrollSpeed = speed; }
    void setButtonCount(int count) { m_buttonCount = qBound(0, count, 3); }
    void setThreeFingerSwipe(bool enabled) { m_threeFingerSwipeEnabled = enabled; }
    void setFourFingerShowDesktop(bool enabled) { m_fourFingerShowDesktopEnabled = enabled; }
    void setPinchZoom(bool enabled) { m_pinchZoomEnabled = enabled; }
    void setTapToClick(bool enabled) { m_tapToClickEnabled = enabled; }
    void setTwoFingerRightClick(bool enabled) { m_twoFingerRightClickEnabled = enabled; }
    void setTwoFingerRotate(bool enabled) { m_rotateEnabled = enabled; }
    void setInvertScroll(bool enabled) { m_invertScroll = enabled; }

    bool start();
    void stop();

Q_SIGNALS:
    void deviceError();
    void buttonHighlightChanged(int buttonIndex); // -1 = no highlight
    void showDesktopRequested();
    void restoreWindowsRequested();

private Q_SLOTS:
    void touchReadyRead(int fd);

private:
    struct Slot {
        int id = -1;
        bool active = false;
        int x = 0, y = 0;
        int prevX = 0, prevY = 0;
        int startX = 0, startY = 0;
        bool firstMove = true;
        bool inButtonStrip = false;
        int buttonIndex = -1;
    };

    bool openTouchscreen();
    bool openUinput();
    void closeFds();
    void disableLibinputAcceleration(const QString &eventNode, int attempt);
    void processSyncReport();
    void processOneFingerMove(int activeCount, int primarySlot, bool secondaryIsButton,
                              bool primaryInTouchpad, bool primaryInCenter);
    void processTwoFingerGesture(int primarySlot, int secondarySlot);
    void processThreeFingerSwipe(int activeCount);
    void processFourFingerSwipe(int activeCount);
    void recoverFromDrop();
    QPointF cursorDelta(const Slot &s);
    void pushMotionSample(double x, double y);
    double cursorSpeedMmPerSec() const;
    void handleFingerLift(int slotIdx);
    void emitRelative(int dx, int dy);
    void emitButton(int btn, bool press);
    void emitSync();
    void switchDesktop(int direction);
    void emitCtrlScroll(int hiResUnits);
    void emitDial(int ticks);
    void writeEv(const input_event &ev);


    static constexpr int MAX_SLOTS = 10;
    Slot m_slots[MAX_SLOTS];
    int m_currentSlot = 0;

    QRectF m_touchpadRect;
    int m_screenW = 2880;
    int m_screenH = 1800;
    double m_pointerSpeed = 1.0;
    double m_scrollSpeed = 1.0;
    int m_buttonCount = 3;
    bool m_threeFingerSwipeEnabled = true;
    bool m_fourFingerShowDesktopEnabled = true;
    bool m_pinchZoomEnabled = true;
    bool m_tapToClickEnabled = true;
    bool m_twoFingerRightClickEnabled = true;
    bool m_rotateEnabled = true;
    bool m_invertScroll = false;
    int m_peakActiveCount = 0;
    int m_twoFingerCleanLiftCount = 0;

    bool m_pendingDrop = false;

    int m_touchFd = -1;
    int m_uinputFd = -1;
    QSocketNotifier *m_notifier = nullptr;

    int m_absXMin = 0, m_absXMax = 0;
    int m_absYMin = 0, m_absYMax = 0;

    // Sub-pixel accumulator for pointer movement (reset per session)
    double m_accumDX = 0.0;
    double m_accumDY = 0.0;

    // Pointer acceleration: 0 = linear, 1 = strongest curve.
    double m_pointerAccel = 0.5;
    // Raw touchscreen units per millimetre (from EVIOCGABS resolution).
    double m_rawPerMm = 100.0;
    // Kernel timestamp of the SYN_REPORT currently being processed.
    qint64 m_frameTimeUs = 0;

    // Motion filter for the finger driving the cursor.  The hysteresis point
    // only moves once the finger leaves a small circle around it, so a resting
    // finger's sensor noise is absorbed while slow motion passes through.
    int m_cursorTrackingId = -1;
    double m_hystX = 0.0;
    double m_hystY = 0.0;
    struct MotionSample { qint64 timeUs; double x, y; };
    static constexpr int MOTION_HISTORY = 8;
    MotionSample m_motion[MOTION_HISTORY] = {};
    int m_motionHead = 0;   // next write index
    int m_motionCount = 0;

    // Bumped on every uinput device creation so stale libinput-config retries
    // for a destroyed device stop.
    int m_uinputGeneration = 0;

    // One-frame cursor buffer: movement is held for one sync cycle and only
    // emitted when the finger is confirmed still active the following frame.
    // This suppresses the noisy penultimate-frame position artifact at lift.
    int m_pendingCursorDX = 0;
    int m_pendingCursorDY = 0;

    // Hi-res scroll accumulator (reset per session); units: 120 = one traditional tick
    double m_scrollAccum = 0.0;

    int m_prevActiveCount = 0;
    // Incremented on each finger lift; blocks cursor movement during peel-off so
    // the trailing finger of a 2-finger scroll doesn't jerk the cursor.
    int m_gestureLiftedCount = 0;

    bool m_buttonStripPending = false;  // button strip touched but BTN not yet emitted
    bool m_buttonStripHeld = false;     // BTN is currently held (cursor finger also active)
    int m_buttonStripCode = 0;

    int m_swipeAccumX = 0;
    bool m_swipeFired = false;
    int m_fourFingerAccumY = 0;
    bool m_fourFingerFired = false;
    double m_pinchAccum = 0.0;
    double m_rotateAccum = 0.0;

    enum class TwoFingerGesture { None, Scroll, Pinch, Rotate, Drag };
    TwoFingerGesture m_twoFingerGesture = TwoFingerGesture::None;
    bool m_dragButtonHeld = false;
};

} // namespace PlasmaTP
