#include "TouchpadOverlay.h"
#include "TouchInputHandler.h"
#include "WindowMover.h"
#include "PanelMover.h"
#include "Settings.h"

#include <QQuickView>
#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QUrl>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDebug>

#include <KConfig>
#include <KConfigGroup>

#include <LayerShellQt/Window>

namespace PlasmaTP {

// Bottom screen connector name as reported by KWin / DRM
static constexpr const char *BOTTOM_SCREEN = "eDP-2";

// Pixels trimmed from half-screen height to keep the overlay clear of the keyboard.
// The two dock positions have slightly different physical clearances.
static constexpr int OVERLAY_TRIM_TOP_HALF = 115;  // keyboard on top
static constexpr int OVERLAY_TRIM_BOTTOM_HALF = 110;  // keyboard on bottom

TouchpadOverlay::TouchpadOverlay(QObject *parent)
    : QObject(parent)
    , m_touch(new TouchInputHandler(this))
    , m_windows(new WindowMover(this))
    , m_panels(new PanelMover(this))
{
    watchVirtualKeyboard();
}

TouchpadOverlay::~TouchpadOverlay()
{
    hide();
}

QRectF TouchpadOverlay::overlayRect(DockPosition position) const
{
    const int halfH = m_screenH / 2;
    switch (position) {
    case DockPosition::TopHalf: {
        const int h = halfH - OVERLAY_TRIM_TOP_HALF;
        return QRectF(0, m_screenH - h, m_screenW, h);
    }
    case DockPosition::BottomHalf: {
        const int h = halfH - OVERLAY_TRIM_BOTTOM_HALF;
        return QRectF(0, 0, m_screenW, h);
    }
    default:
        return {};
    }
}

void TouchpadOverlay::show(DockPosition position)
{
    if (m_view)
        hide();

    // Find the bottom screen
    QScreen *targetScreen = nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == QLatin1String(BOTTOM_SCREEN)) {
            targetScreen = s;
            m_screenW = static_cast<int>(s->geometry().width());
            m_screenH = static_cast<int>(s->geometry().height());
            break;
        }
    }
    if (!targetScreen) {
        qWarning() << "TouchpadOverlay: screen" << BOTTOM_SCREEN << "not found";
        return;
    }

    // Load settings from config before creating the QML view so context
    // properties can be set before setSource() triggers QML loading.
    Settings s;
    {
        KConfig cfg(QLatin1String(Settings::configFile()));
        KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));
        s.pointerSpeed = grp.readEntry("pointerSpeed", s.pointerSpeed);
        s.scrollSpeed = grp.readEntry("scrollSpeed", s.scrollSpeed);
        s.tapToClick = grp.readEntry("tapToClick", s.tapToClick);
        s.buttonCount = grp.readEntry("buttonCount", s.buttonCount);
        s.colorScheme = grp.readEntry("colorScheme", s.colorScheme);
        s.gestureRightClick = grp.readEntry("gestureRightClick", s.gestureRightClick);
        s.gestureSmartZoom = grp.readEntry("gestureSmartZoom", s.gestureSmartZoom);
        s.gestureScroll = grp.readEntry("gestureScroll", s.gestureScroll);
        s.gesturePinchZoom = grp.readEntry("gesturePinchZoom", s.gesturePinchZoom);
        s.gestureRotate = grp.readEntry("gestureRotate", s.gestureRotate);
        s.gestureSwipePages = grp.readEntry("gestureSwipePages", s.gestureSwipePages);
        s.gestureSwipeDesktops = grp.readEntry("gestureSwipeDesktops", s.gestureSwipeDesktops);
    }

    // Move windows and panels to eDP-1 BEFORE creating the overlay surface.
    // This lets KWin complete its active-screen reassignment now, so it has
    // no reason to reassign the overlay once it appears on eDP-2.
    m_windows->moveWindowsToTopScreen();
    m_panels->movePanelsToTopScreen();
    setVirtualKeyboardEnabled(false);

    // Full-screen black layer on eDP-2, sits behind the touchpad overlay.
    // LayerTop (2) is below LayerOverlay (3), so the touchpad renders on top.
    m_blackout = new QQuickView();
    m_blackout->setResizeMode(QQuickView::SizeRootObjectToView);
    m_blackout->setSource(QUrl(QStringLiteral("qrc:/org/kde/plasma/touchpad/qml/Blackout.qml")));
    m_blackout->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_blackout->setScreen(targetScreen);

    m_blackoutLayerWindow = LayerShellQt::Window::get(m_blackout);
    m_blackoutLayerWindow->setLayer(LayerShellQt::Window::LayerTop);
    m_blackoutLayerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_blackoutLayerWindow->setScope(QStringLiteral("plasma-touchpad-blackout"));
    m_blackoutLayerWindow->setExclusiveZone(-1);
    m_blackoutLayerWindow->setScreen(targetScreen);
    m_blackoutLayerWindow->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight |
        LayerShellQt::Window::AnchorTop |
        LayerShellQt::Window::AnchorBottom));
    m_blackoutLayerWindow->setDesiredSize(QSize(0, 0)); // compositor fills all-anchored surface

    connect(m_blackout, &QWindow::screenChanged, this, [this](QScreen *s) {
        if (!m_blackoutLayerWindow || !s || s->name() == QLatin1String(BOTTOM_SCREEN))
            return;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == QLatin1String(BOTTOM_SCREEN)) {
                m_blackoutLayerWindow->setScreen(screen);
                return;
            }
        }
    });

    m_blackout->show();

    // Create the QML overlay window.
    // Context properties must be set before setSource() triggers QML loading.
    m_view = new QQuickView();
    m_view->rootContext()->setContextProperty(QStringLiteral("buttonCount"), s.buttonCount);
    m_view->rootContext()->setContextProperty(QStringLiteral("colorScheme"), s.colorScheme);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setSource(QUrl(QStringLiteral("qrc:/org/kde/plasma/touchpad/qml/Touchpad.qml")));
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Pin Qt's own screen tracking to eDP-2 before LayerShellQt attaches.
    // LayerShellQt falls back to QWindow::screen() when its own screen property
    // is not set, so both must agree to ensure the overlay lands on eDP-2
    // regardless of which screen is currently active/primary.
    m_view->setScreen(targetScreen);

    // Configure as a LayerShell overlay surface pinned to eDP-2.
    m_layerWindow = LayerShellQt::Window::get(m_view);
    auto *layerWindow = m_layerWindow;
    layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    layerWindow->setScope(QStringLiteral("plasma-touchpad"));
    layerWindow->setExclusiveZone(-1); // don't push other surfaces away
    // Explicitly pin the LayerShell surface to eDP-2.  setScreen() stores the
    // QScreen* in LayerShellQt::WindowPrivate and uses it directly when the
    // zwlr_layer_surface_v1 is created at show() time, avoiding the "placeholder
    // screen" path that QWindow::screen() can fall into before the compositor
    // has fully mapped the output.
    layerWindow->setScreen(targetScreen);

    // TopHalf dock  → overlay on bottom half → anchor bottom
    // BottomHalf dock → overlay on top half  → anchor top
    const bool overlayOnTop = (position == DockPosition::BottomHalf);
    LayerShellQt::Window::Anchor vertAnchor = overlayOnTop
        ? LayerShellQt::Window::AnchorTop
        : LayerShellQt::Window::AnchorBottom;
    layerWindow->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight |
        vertAnchor));
    const int trim = (position == DockPosition::BottomHalf) ? OVERLAY_TRIM_BOTTOM_HALF
                                                              : OVERLAY_TRIM_TOP_HALF;
    layerWindow->setDesiredSize(QSize(0, m_screenH / 2 - trim));

    connect(m_view, &QWindow::screenChanged, this, [this](QScreen *s) {
        const QString name = s ? s->name() : QStringLiteral("(null)");
        qDebug() << "TouchpadOverlay: overlay screen changed to" << name;
        if (!m_layerWindow || !s || s->name() == QLatin1String(BOTTOM_SCREEN))
            return;
        // KWin moved the overlay off eDP-2 (probably because it reassigned the
        // active screen after windows were moved).  Re-pin by calling setScreen
        // again — this triggers LayerShellQt to recreate the surface on eDP-2.
        qDebug() << "TouchpadOverlay: re-pinning overlay back to" << BOTTOM_SCREEN;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->name() == QLatin1String(BOTTOM_SCREEN)) {
                m_layerWindow->setScreen(screen);
                return;
            }
        }
        qWarning() << "TouchpadOverlay: eDP-2 not found in screen list, cannot re-pin";
    });

    m_view->show();

    // Apply settings to the input handler
    const QRectF touchRect = overlayRect(position);
    m_touch->setScreenSize(m_screenW, m_screenH);
    m_touch->setTouchpadRect(touchRect);
    m_touch->setPointerSpeed(s.pointerSpeed);
    m_touch->setScrollSpeed(s.scrollSpeed);
    m_touch->setTapToClick(s.tapToClick);
    m_touch->setButtonCount(s.buttonCount);
    m_touch->setGestureRightClick(s.gestureRightClick);
    m_touch->setGestureSmartZoom(s.gestureSmartZoom);
    m_touch->setGestureScroll(s.gestureScroll);
    m_touch->setGesturePinchZoom(s.gesturePinchZoom);
    m_touch->setGestureRotate(s.gestureRotate);
    m_touch->setGestureSwipePages(s.gestureSwipePages);
    m_touch->setGestureSwipeDesktops(s.gestureSwipeDesktops);
    if (!m_touch->start())
        qWarning() << "TouchpadOverlay: failed to start touch input handler";

    qDebug() << "TouchpadOverlay: shown on" << BOTTOM_SCREEN
             << "position" << static_cast<int>(position)
             << "touchRect" << touchRect;
}

void TouchpadOverlay::hide()
{
    if (!m_view)
        return;

    m_touch->stop();
    m_touch->setTouchpadRect(QRectF());
    m_windows->restore();
    m_panels->restorePanels();
    setVirtualKeyboardEnabled(true);

    m_blackoutLayerWindow = nullptr;
    m_blackout->hide();
    delete m_blackout;
    m_blackout = nullptr;

    m_layerWindow = nullptr;
    m_view->hide();
    delete m_view;
    m_view = nullptr;

    qDebug() << "TouchpadOverlay: hidden";
}

void TouchpadOverlay::setVirtualKeyboardEnabled(bool enabled)
{
    QDBusInterface iface(QStringLiteral("org.kde.KWin"),
                         QStringLiteral("/VirtualKeyboard"),
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) {
        qWarning() << "TouchpadOverlay: KWin VirtualKeyboard DBus interface not available";
        return;
    }
    iface.call(QStringLiteral("Set"),
               QStringLiteral("org.kde.kwin.VirtualKeyboard"),
               QStringLiteral("enabled"),
               QVariant::fromValue(QDBusVariant(enabled)));
    qDebug() << "TouchpadOverlay: virtual keyboard" << (enabled ? "enabled" : "disabled");
}

void TouchpadOverlay::watchVirtualKeyboard()
{
    // Listen for KWin VirtualKeyboard property changes. If something re-enables
    // the OSK while our overlay is visible, we immediately suppress it again.
    QDBusConnection::sessionBus().connect(
        QStringLiteral("org.kde.KWin"),
        QStringLiteral("/VirtualKeyboard"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this,
        SLOT(onVirtualKeyboardPropertyChanged(QString,QVariantMap,QStringList)));
}

void TouchpadOverlay::onVirtualKeyboardPropertyChanged(const QString &iface,
                                                        const QVariantMap &changed,
                                                        const QStringList &)
{
    if (iface != QLatin1String("org.kde.kwin.VirtualKeyboard"))
        return;
    if (!m_view)
        return;  // overlay not visible — don't interfere

    const auto it = changed.find(QStringLiteral("enabled"));
    if (it != changed.end() && it->toBool()) {
        qDebug() << "TouchpadOverlay: suppressing OSK re-enable while touchpad is active";
        setVirtualKeyboardEnabled(false);
    }
}

} // namespace PlasmaTP
