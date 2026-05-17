#pragma once

#include <QObject>
#include <QRectF>
#include <QSocketNotifier>

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
    void setScrollSpeed(double speed) { m_scrollSpeed = speed; }
    void setButtonCount(int count) { m_buttonCount = qBound(0, count, 3); }
    void setThreeFingerSwipe(bool enabled) { m_threeFingerSwipeEnabled = enabled; }
    void setPinchZoom(bool enabled) { m_pinchZoomEnabled = enabled; }
    void setTapToClick(bool enabled) { m_tapToClickEnabled = enabled; }
    void setTwoFingerRightClick(bool enabled) { m_twoFingerRightClickEnabled = enabled; }
    void setTwoFingerRotate(bool enabled) { m_rotateEnabled = enabled; }

    bool start();
    void stop();

private Q_SLOTS:
    void touchReadyRead(int fd);

private:
    bool openTouchscreen();
    bool openUinput();
    void closeFds();
    void processSyncReport();
    void recoverFromDrop();
    void handleFingerLift(int slotIdx);
    void emitRelative(int dx, int dy);
    void emitButton(int btn, bool press);
    void emitSync();
    void switchDesktop(int direction);
    void emitCtrlScroll(int hiResUnits);
    void emitDial(int ticks);

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
    bool m_pinchZoomEnabled = true;
    bool m_tapToClickEnabled = true;
    bool m_twoFingerRightClickEnabled = true;
    bool m_rotateEnabled = true;
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
    double m_pinchAccum = 0.0;
    double m_rotateAccum = 0.0;
};

} // namespace PlasmaTP
