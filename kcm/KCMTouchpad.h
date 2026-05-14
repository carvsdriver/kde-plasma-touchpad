#pragma once

#include <KQuickConfigModule>

namespace PlasmaTP {

class KCMTouchpad : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(double pointerSpeed READ pointerSpeed WRITE setPointerSpeed NOTIFY pointerSpeedChanged)
    Q_PROPERTY(double scrollSpeed READ scrollSpeed WRITE setScrollSpeed NOTIFY scrollSpeedChanged)
    Q_PROPERTY(bool tapToClick READ tapToClick WRITE setTapToClick NOTIFY tapToClickChanged)
    Q_PROPERTY(bool movePanels READ movePanels WRITE setMovePanels NOTIFY movePanelsChanged)
    Q_PROPERTY(int activateOnMask READ activateOnMask WRITE setActivateOnMask NOTIFY activateOnMaskChanged)
    Q_PROPERTY(int buttonCount READ buttonCount WRITE setButtonCount NOTIFY buttonCountChanged)
    Q_PROPERTY(QString colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)

    Q_PROPERTY(bool gestureRightClick    READ gestureRightClick    WRITE setGestureRightClick    NOTIFY gestureRightClickChanged)
    Q_PROPERTY(bool gestureSmartZoom     READ gestureSmartZoom     WRITE setGestureSmartZoom     NOTIFY gestureSmartZoomChanged)
    Q_PROPERTY(bool gestureScroll        READ gestureScroll        WRITE setGestureScroll        NOTIFY gestureScrollChanged)
    Q_PROPERTY(bool gesturePinchZoom     READ gesturePinchZoom     WRITE setGesturePinchZoom     NOTIFY gesturePinchZoomChanged)
    Q_PROPERTY(bool gestureRotate        READ gestureRotate        WRITE setGestureRotate        NOTIFY gestureRotateChanged)
    Q_PROPERTY(bool gestureSwipePages    READ gestureSwipePages    WRITE setGestureSwipePages    NOTIFY gestureSwipePagesChanged)
    Q_PROPERTY(bool gestureSwipeDesktops READ gestureSwipeDesktops WRITE setGestureSwipeDesktops NOTIFY gestureSwipeDesktopsChanged)

public:
    explicit KCMTouchpad(QObject *parent, const KPluginMetaData &data);

    void load() override;
    void save() override;
    void defaults() override;

    double pointerSpeed() const { return m_pointerSpeed; }
    double scrollSpeed() const { return m_scrollSpeed; }
    bool tapToClick() const { return m_tapToClick; }
    bool movePanels() const { return m_movePanels; }
    int activateOnMask() const { return m_activateOnMask; }
    int buttonCount() const { return m_buttonCount; }
    QString colorScheme() const { return m_colorScheme; }

    bool gestureRightClick()    const { return m_gestureRightClick; }
    bool gestureSmartZoom()     const { return m_gestureSmartZoom; }
    bool gestureScroll()        const { return m_gestureScroll; }
    bool gesturePinchZoom()     const { return m_gesturePinchZoom; }
    bool gestureRotate()        const { return m_gestureRotate; }
    bool gestureSwipePages()    const { return m_gestureSwipePages; }
    bool gestureSwipeDesktops() const { return m_gestureSwipeDesktops; }

    void setPointerSpeed(double v) { if (m_pointerSpeed != v) { m_pointerSpeed = v; Q_EMIT pointerSpeedChanged(); } }
    void setScrollSpeed(double v) { if (m_scrollSpeed != v) { m_scrollSpeed = v; Q_EMIT scrollSpeedChanged(); } }
    void setTapToClick(bool v) { if (m_tapToClick != v) { m_tapToClick = v; Q_EMIT tapToClickChanged(); } }
    void setMovePanels(bool v) { if (m_movePanels != v) { m_movePanels = v; Q_EMIT movePanelsChanged(); } }
    void setActivateOnMask(int v) { if (m_activateOnMask != v) { m_activateOnMask = v; Q_EMIT activateOnMaskChanged(); } }
    void setButtonCount(int v) { if (m_buttonCount != v) { m_buttonCount = v; Q_EMIT buttonCountChanged(); } }
    void setColorScheme(const QString &v) { if (m_colorScheme != v) { m_colorScheme = v; Q_EMIT colorSchemeChanged(); } }

    void setGestureRightClick(bool v)    { if (m_gestureRightClick    != v) { m_gestureRightClick    = v; Q_EMIT gestureRightClickChanged(); } }
    void setGestureSmartZoom(bool v)     { if (m_gestureSmartZoom     != v) { m_gestureSmartZoom     = v; Q_EMIT gestureSmartZoomChanged(); } }
    void setGestureScroll(bool v)        { if (m_gestureScroll        != v) { m_gestureScroll        = v; Q_EMIT gestureScrollChanged(); } }
    void setGesturePinchZoom(bool v)     { if (m_gesturePinchZoom     != v) { m_gesturePinchZoom     = v; Q_EMIT gesturePinchZoomChanged(); } }
    void setGestureRotate(bool v)        { if (m_gestureRotate        != v) { m_gestureRotate        = v; Q_EMIT gestureRotateChanged(); } }
    void setGestureSwipePages(bool v)    { if (m_gestureSwipePages    != v) { m_gestureSwipePages    = v; Q_EMIT gestureSwipePagesChanged(); } }
    void setGestureSwipeDesktops(bool v) { if (m_gestureSwipeDesktops != v) { m_gestureSwipeDesktops = v; Q_EMIT gestureSwipeDesktopsChanged(); } }

Q_SIGNALS:
    void pointerSpeedChanged();
    void scrollSpeedChanged();
    void tapToClickChanged();
    void movePanelsChanged();
    void activateOnMaskChanged();
    void buttonCountChanged();
    void colorSchemeChanged();

    void gestureRightClickChanged();
    void gestureSmartZoomChanged();
    void gestureScrollChanged();
    void gesturePinchZoomChanged();
    void gestureRotateChanged();
    void gestureSwipePagesChanged();
    void gestureSwipeDesktopsChanged();

private:
    double m_pointerSpeed = 1.0;
    double m_scrollSpeed = 1.0;
    bool m_tapToClick = true;
    bool m_movePanels = true;
    int m_activateOnMask = 0x3;
    int m_buttonCount = 3;
    QString m_colorScheme = QStringLiteral("system");

    bool m_gestureRightClick    = true;
    bool m_gestureSmartZoom     = true;
    bool m_gestureScroll        = true;
    bool m_gesturePinchZoom     = true;
    bool m_gestureRotate        = true;
    bool m_gestureSwipePages    = true;
    bool m_gestureSwipeDesktops = true;
};

} // namespace PlasmaTP
