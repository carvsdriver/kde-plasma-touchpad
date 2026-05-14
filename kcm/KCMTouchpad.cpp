#include "KCMTouchpad.h"

#include <KPluginFactory>
#include <KConfig>
#include <KConfigGroup>

#include "../common/Settings.h"

namespace PlasmaTP {

K_PLUGIN_CLASS_WITH_JSON(KCMTouchpad, "kcm_plasma_touchpad.json")

KCMTouchpad::KCMTouchpad(QObject *parent, const KPluginMetaData &data)
    : KQuickConfigModule(parent, data)
{
    setButtons(Apply | Default);
}

void KCMTouchpad::load()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));

    Settings s;
    setPointerSpeed(grp.readEntry("pointerSpeed", s.pointerSpeed));
    setScrollSpeed(grp.readEntry("scrollSpeed", s.scrollSpeed));
    setTapToClick(grp.readEntry("tapToClick", s.tapToClick));
    setMovePanels(grp.readEntry("movePanels", s.movePanels));
    setActivateOnMask(grp.readEntry("activateOnMask", s.activateOnMask));
    setButtonCount(grp.readEntry("buttonCount", s.buttonCount));
    setColorScheme(grp.readEntry("colorScheme", s.colorScheme));
    setGestureRightClick(grp.readEntry("gestureRightClick",       s.gestureRightClick));
    setGestureSmartZoom(grp.readEntry("gestureSmartZoom",         s.gestureSmartZoom));
    setGestureScroll(grp.readEntry("gestureScroll",               s.gestureScroll));
    setGesturePinchZoom(grp.readEntry("gesturePinchZoom",         s.gesturePinchZoom));
    setGestureRotate(grp.readEntry("gestureRotate",               s.gestureRotate));
    setGestureSwipePages(grp.readEntry("gestureSwipePages",       s.gestureSwipePages));
    setGestureSwipeDesktops(grp.readEntry("gestureSwipeDesktops", s.gestureSwipeDesktops));
    setNeedsSave(false);
}

void KCMTouchpad::save()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));

    grp.writeEntry("pointerSpeed", m_pointerSpeed);
    grp.writeEntry("scrollSpeed", m_scrollSpeed);
    grp.writeEntry("tapToClick", m_tapToClick);
    grp.writeEntry("movePanels", m_movePanels);
    grp.writeEntry("activateOnMask", m_activateOnMask);
    grp.writeEntry("buttonCount", m_buttonCount);
    grp.writeEntry("colorScheme",         m_colorScheme);
    grp.writeEntry("gestureRightClick",    m_gestureRightClick);
    grp.writeEntry("gestureSmartZoom",     m_gestureSmartZoom);
    grp.writeEntry("gestureScroll",        m_gestureScroll);
    grp.writeEntry("gesturePinchZoom",     m_gesturePinchZoom);
    grp.writeEntry("gestureRotate",        m_gestureRotate);
    grp.writeEntry("gestureSwipePages",    m_gestureSwipePages);
    grp.writeEntry("gestureSwipeDesktops", m_gestureSwipeDesktops);
    cfg.sync();
    setNeedsSave(false);
}

void KCMTouchpad::defaults()
{
    Settings s;
    setPointerSpeed(s.pointerSpeed);
    setScrollSpeed(s.scrollSpeed);
    setTapToClick(s.tapToClick);
    setMovePanels(s.movePanels);
    setActivateOnMask(s.activateOnMask);
    setButtonCount(s.buttonCount);
    setColorScheme(s.colorScheme);
    setGestureRightClick(s.gestureRightClick);
    setGestureSmartZoom(s.gestureSmartZoom);
    setGestureScroll(s.gestureScroll);
    setGesturePinchZoom(s.gesturePinchZoom);
    setGestureRotate(s.gestureRotate);
    setGestureSwipePages(s.gestureSwipePages);
    setGestureSwipeDesktops(s.gestureSwipeDesktops);
    setNeedsSave(true);
}

} // namespace PlasmaTP

#include "KCMTouchpad.moc"
