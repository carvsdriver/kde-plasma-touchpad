#include "KCMTouchpad.h"

#include <KPluginFactory>
#include <KConfig>
#include <KConfigGroup>

#include <QDBusConnection>
#include <QDBusMessage>

#include "../common/Settings.h"

namespace PlasmaTP {

K_PLUGIN_CLASS_WITH_JSON(KCMTouchpad, "kcm_yoga9_onscreen_touchpad.json")

KCMTouchpad::KCMTouchpad(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
{
}

void KCMTouchpad::load()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));
    Settings s;
    setColorScheme(grp.readEntry("colorScheme", s.colorScheme));
    setButtonCount(grp.readEntry("buttonCount", s.buttonCount));
    setPointerSpeed(grp.readEntry("pointerSpeed", s.pointerSpeed));
    setScrollSpeed(grp.readEntry("scrollSpeed", s.scrollSpeed));
    setDisableRotation(grp.readEntry("disableRotation", s.disableRotation));
    setThreeFingerSwipe(grp.readEntry("threeFingerSwipe", s.threeFingerSwipe));
    setFourFingerShowDesktop(grp.readEntry("fourFingerShowDesktop", s.fourFingerShowDesktop));
    setPinchZoom(grp.readEntry("pinchZoom", s.pinchZoom));
    setTapToClick(grp.readEntry("tapToClick", s.tapToClick));
    setTwoFingerRightClick(grp.readEntry("twoFingerRightClick", s.twoFingerRightClick));
    setTwoFingerRotate(grp.readEntry("twoFingerRotate", s.twoFingerRotate));
    setInvertScroll(grp.readEntry("invertScroll", s.invertScroll));
    setNeedsSave(false);
}

void KCMTouchpad::save()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));
    grp.writeEntry("colorScheme", m_colorScheme);
    grp.writeEntry("buttonCount", m_buttonCount);
    grp.writeEntry("pointerSpeed", m_pointerSpeed);
    grp.writeEntry("scrollSpeed", m_scrollSpeed);
    grp.writeEntry("disableRotation", m_disableRotation);
    grp.writeEntry("threeFingerSwipe", m_threeFingerSwipe);
    grp.writeEntry("fourFingerShowDesktop", m_fourFingerShowDesktop);
    grp.writeEntry("pinchZoom", m_pinchZoom);
    grp.writeEntry("tapToClick", m_tapToClick);
    grp.writeEntry("twoFingerRightClick", m_twoFingerRightClick);
    grp.writeEntry("twoFingerRotate", m_twoFingerRotate);
    grp.writeEntry("invertScroll", m_invertScroll);
    cfg.sync();
    setNeedsSave(false);

    QDBusMessage sig = QDBusMessage::createSignal(
        QStringLiteral("/org/kde/plasma/touchpad"),
        QStringLiteral("org.kde.plasma.touchpad"),
        QStringLiteral("configChanged"));
    QDBusConnection::sessionBus().send(sig);
}

void KCMTouchpad::defaults()
{
    Settings s;
    setColorScheme(s.colorScheme);
    setButtonCount(s.buttonCount);
    setPointerSpeed(s.pointerSpeed);
    setScrollSpeed(s.scrollSpeed);
    setDisableRotation(s.disableRotation);
    setThreeFingerSwipe(s.threeFingerSwipe);
    setFourFingerShowDesktop(s.fourFingerShowDesktop);
    setPinchZoom(s.pinchZoom);
    setTapToClick(s.tapToClick);
    setTwoFingerRightClick(s.twoFingerRightClick);
    setTwoFingerRotate(s.twoFingerRotate);
    setInvertScroll(s.invertScroll);
    setNeedsSave(true);
}

} // namespace PlasmaTP

#include "KCMTouchpad.moc"
