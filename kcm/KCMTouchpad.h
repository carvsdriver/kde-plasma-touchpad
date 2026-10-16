#pragma once

#include <KQuickConfigModule>
#include <QString>

namespace PlasmaTP {

class KCMTouchpad : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(QString colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)
    Q_PROPERTY(int buttonCount READ buttonCount WRITE setButtonCount NOTIFY buttonCountChanged)
    Q_PROPERTY(double pointerSpeed READ pointerSpeed WRITE setPointerSpeed NOTIFY pointerSpeedChanged)
    Q_PROPERTY(double pointerAcceleration READ pointerAcceleration WRITE setPointerAcceleration NOTIFY pointerAccelerationChanged)
    Q_PROPERTY(double scrollSpeed READ scrollSpeed WRITE setScrollSpeed NOTIFY scrollSpeedChanged)
    Q_PROPERTY(bool disableRotation READ disableRotation WRITE setDisableRotation NOTIFY disableRotationChanged)
    Q_PROPERTY(bool threeFingerSwipe READ threeFingerSwipe WRITE setThreeFingerSwipe NOTIFY threeFingerSwipeChanged)
    Q_PROPERTY(bool fourFingerShowDesktop READ fourFingerShowDesktop WRITE setFourFingerShowDesktop NOTIFY fourFingerShowDesktopChanged)
    Q_PROPERTY(bool pinchZoom READ pinchZoom WRITE setPinchZoom NOTIFY pinchZoomChanged)
    Q_PROPERTY(bool tapToClick READ tapToClick WRITE setTapToClick NOTIFY tapToClickChanged)
    Q_PROPERTY(bool twoFingerRightClick READ twoFingerRightClick WRITE setTwoFingerRightClick NOTIFY twoFingerRightClickChanged)
    Q_PROPERTY(bool twoFingerRotate READ twoFingerRotate WRITE setTwoFingerRotate NOTIFY twoFingerRotateChanged)
    Q_PROPERTY(bool invertScroll READ invertScroll WRITE setInvertScroll NOTIFY invertScrollChanged)

public:
    explicit KCMTouchpad(QObject *parent, const KPluginMetaData &data);

    void load() override;
    void save() override;
    void defaults() override;

    QString colorScheme() const { return m_colorScheme; }
    int buttonCount() const { return m_buttonCount; }
    double pointerSpeed() const { return m_pointerSpeed; }
    double pointerAcceleration() const { return m_pointerAcceleration; }
    double scrollSpeed() const { return m_scrollSpeed; }
    bool disableRotation() const { return m_disableRotation; }
    bool threeFingerSwipe() const { return m_threeFingerSwipe; }
    bool fourFingerShowDesktop() const { return m_fourFingerShowDesktop; }
    bool pinchZoom() const { return m_pinchZoom; }
    bool tapToClick() const { return m_tapToClick; }
    bool twoFingerRightClick() const { return m_twoFingerRightClick; }
    bool twoFingerRotate() const { return m_twoFingerRotate; }
    bool invertScroll() const { return m_invertScroll; }
    void setColorScheme(const QString &v) { if (m_colorScheme != v) { m_colorScheme = v; Q_EMIT colorSchemeChanged(); } }
    void setButtonCount(int v) { if (m_buttonCount != v) { m_buttonCount = v; Q_EMIT buttonCountChanged(); } }
    void setPointerSpeed(double v) { if (m_pointerSpeed != v) { m_pointerSpeed = v; Q_EMIT pointerSpeedChanged(); } }
    void setPointerAcceleration(double v) { if (m_pointerAcceleration != v) { m_pointerAcceleration = v; Q_EMIT pointerAccelerationChanged(); } }
    void setScrollSpeed(double v) { if (m_scrollSpeed != v) { m_scrollSpeed = v; Q_EMIT scrollSpeedChanged(); } }
    void setDisableRotation(bool v) { if (m_disableRotation != v) { m_disableRotation = v; Q_EMIT disableRotationChanged(); } }
    void setThreeFingerSwipe(bool v) { if (m_threeFingerSwipe != v) { m_threeFingerSwipe = v; Q_EMIT threeFingerSwipeChanged(); } }
    void setFourFingerShowDesktop(bool v) { if (m_fourFingerShowDesktop != v) { m_fourFingerShowDesktop = v; Q_EMIT fourFingerShowDesktopChanged(); } }
    void setPinchZoom(bool v) { if (m_pinchZoom != v) { m_pinchZoom = v; Q_EMIT pinchZoomChanged(); } }
    void setTapToClick(bool v) { if (m_tapToClick != v) { m_tapToClick = v; Q_EMIT tapToClickChanged(); } }
    void setTwoFingerRightClick(bool v) { if (m_twoFingerRightClick != v) { m_twoFingerRightClick = v; Q_EMIT twoFingerRightClickChanged(); } }
    void setTwoFingerRotate(bool v) { if (m_twoFingerRotate != v) { m_twoFingerRotate = v; Q_EMIT twoFingerRotateChanged(); } }
    void setInvertScroll(bool v) { if (m_invertScroll != v) { m_invertScroll = v; Q_EMIT invertScrollChanged(); } }

Q_SIGNALS:
    void colorSchemeChanged();
    void buttonCountChanged();
    void pointerSpeedChanged();
    void pointerAccelerationChanged();
    void scrollSpeedChanged();
    void disableRotationChanged();
    void threeFingerSwipeChanged();
    void fourFingerShowDesktopChanged();
    void pinchZoomChanged();
    void tapToClickChanged();
    void twoFingerRightClickChanged();
    void twoFingerRotateChanged();
    void invertScrollChanged();

private:
    QString m_colorScheme = QStringLiteral("system");
    int m_buttonCount = 3;
    double m_pointerSpeed = 0.3;
    double m_pointerAcceleration = 0.5;
    double m_scrollSpeed = 1.0;
    bool m_disableRotation = true;
    bool m_threeFingerSwipe = true;
    bool m_fourFingerShowDesktop = true;
    bool m_pinchZoom = true;
    bool m_tapToClick = true;
    bool m_twoFingerRightClick = true;
    bool m_twoFingerRotate = true;
    bool m_invertScroll = false;
};

} // namespace PlasmaTP
