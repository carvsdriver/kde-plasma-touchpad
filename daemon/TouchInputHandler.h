#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
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
    void setTapToClick(bool enabled) { m_tapToClick = enabled; }
    void setButtonCount(int count) { m_buttonCount = qBound(0, count, 3); }

    void setGestureRightClick(bool v) { m_gestureRightClick = v; }
    void setGestureSmartZoom(bool v) { m_gestureSmartZoom = v; }
    void setGestureScroll(bool v) { m_gestureScroll = v; }
    void setGesturePinchZoom(bool v) { m_gesturePinchZoom = v; }
    void setGestureRotate(bool v) { m_gestureRotate = v; }
    void setGestureSwipePages(bool v) { m_gestureSwipePages = v; }
    void setGestureSwipeDesktops(bool v) { m_gestureSwipeDesktops = v; }

    bool start();
    void stop();

private Q_SLOTS:
    void touchReadyRead(int fd);

private:
    bool openTouchscreen();
    bool openUinput();
    bool openUinputKeyboard();
    void closeFds();
    void processSyncReport();
    void recoverFromDrop();
    void handleFingerLift(int slotIdx);
    void emitRelative(int dx, int dy);
    void emitWheel(int ticks);
    void emitButton(int btn, bool press);
    void emitKeyDown(int key);
    void emitKeyUp(int key);
    void emitKey(int key);
    void emitKeyCombo(int mod, int key);
    void emitKeyCombo(int mod1, int mod2, int key);
    void emitSync();

    struct Slot {
        int id = -1;
        bool active = false;
        int x = 0, y = 0;
        int prevX = 0, prevY  = 0;
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
    bool m_tapToClick = true;
    int m_buttonCount = 3;

    bool m_gestureRightClick = true;
    bool m_gestureSmartZoom = true;
    bool m_gestureScroll = true;
    bool m_gesturePinchZoom = true;
    bool m_gestureRotate = true;
    bool m_gestureSwipePages = true;
    bool m_gestureSwipeDesktops = true;

    bool m_pendingDrop = false;
    int m_syncCount = 0;

    int m_touchFd = -1;
    int m_uinputFd = -1;      // pointer device (REL + BTN only)
    int m_uinputKbdFd = -1;   // separate keyboard device (KEY_* only)
    QSocketNotifier *m_notifier = nullptr;

    int m_absXMin = 0, m_absXMax = 0;
    int m_absYMin = 0, m_absYMax = 0;

    // Multi-finger gesture state (reset at start of each gesture session)
    enum class MultiGesture { Unknown, Scroll, Pinch, HorizSwipe };
    MultiGesture m_multiGesture = MultiGesture::Unknown;
    double m_pinchLastDist = 0.0;
    double m_pinchAccum = 0.0;
    double m_hswipeAccum = 0.0;
    double m_fourSwipeAccum = 0.0;
    int m_prevActiveCount = 0;
    int m_peakActiveCount = 0;
    int m_gestureLiftedCount = 0;
    bool m_gestureAnyFarMove = false;
    QElapsedTimer m_gestureSessionTimer;

    // Double-tap tracking (persists across sessions)
    bool m_awaitingDoubleTap = false;
    QElapsedTimer m_lastTwoTapTimer;
};

} // namespace PlasmaTP
