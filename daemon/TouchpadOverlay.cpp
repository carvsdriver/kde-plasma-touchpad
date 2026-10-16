#include "TouchpadOverlay.h"
#include "TouchInputHandler.h"
#include "WindowMover.h"
#include "PanelMover.h"
#include "Settings.h"

#include <QProcess>
#include <QQuickItem>
#include <QQuickView>
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

static QScreen *findBottomScreen()
{
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == QLatin1String(BOTTOM_SCREEN))
            return s;
    }
    return nullptr;
}

static Settings loadFromConfig()
{
    Settings s;
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(Settings::groupName()));
    s.buttonCount = grp.readEntry("buttonCount", s.buttonCount);
    s.colorScheme = grp.readEntry("colorScheme", s.colorScheme);
    s.pointerSpeed = grp.readEntry("pointerSpeed", s.pointerSpeed);
    s.pointerAcceleration = grp.readEntry("pointerAcceleration", s.pointerAcceleration);
    s.scrollSpeed = grp.readEntry("scrollSpeed", s.scrollSpeed);
    s.disableRotation = grp.readEntry("disableRotation", s.disableRotation);
    s.threeFingerSwipe = grp.readEntry("threeFingerSwipe", s.threeFingerSwipe);
    s.fourFingerShowDesktop = grp.readEntry("fourFingerShowDesktop", s.fourFingerShowDesktop);
    s.pinchZoom = grp.readEntry("pinchZoom", s.pinchZoom);
    s.tapToClick = grp.readEntry("tapToClick", s.tapToClick);
    s.twoFingerRightClick = grp.readEntry("twoFingerRightClick", s.twoFingerRightClick);
    s.twoFingerRotate = grp.readEntry("twoFingerRotate", s.twoFingerRotate);
    s.invertScroll = grp.readEntry("invertScroll", s.invertScroll);
    return s;
}

TouchpadOverlay::TouchpadOverlay(QObject *parent)
    : QObject(parent)
    , m_touch(new TouchInputHandler(this))
    , m_windows(new WindowMover(this))
    , m_panels(new PanelMover(this))
{
    connect(m_touch, &TouchInputHandler::deviceError, this, [this]() {
        qWarning() << "TouchpadOverlay: touch device lost, will restart";
        m_touch->stop();
        m_touchRetryTimer->start();
    }, Qt::QueuedConnection);

    // Button strip visual feedback: highlight on press, clear 150 ms after
    // release so taps are visible even though press+release arrive in one frame.
    m_buttonHighlightTimer = new QTimer(this);
    m_buttonHighlightTimer->setSingleShot(true);
    m_buttonHighlightTimer->setInterval(150);
    connect(m_buttonHighlightTimer, &QTimer::timeout, this, [this]() {
        if (m_view && m_view->rootObject())
            m_view->rootObject()->setProperty("pressedButtonIndex", -1);
    });
    connect(m_touch, &TouchInputHandler::buttonHighlightChanged, this, [this](int idx) {
        if (!m_view || !m_view->rootObject()) return;
        if (idx >= 0) {
            m_buttonHighlightTimer->stop();
            m_view->rootObject()->setProperty("pressedButtonIndex", idx);
        } else {
            m_buttonHighlightTimer->start();
        }
    });

    connect(m_touch, &TouchInputHandler::showDesktopRequested,
            m_windows, &WindowMover::showDesktopTopScreen);
    connect(m_touch, &TouchInputHandler::restoreWindowsRequested,
            m_windows, &WindowMover::unminimizeTopScreen);

    m_showTimer = new QTimer(this);
    m_showTimer->setSingleShot(true);
    connect(m_showTimer, &QTimer::timeout, this, &TouchpadOverlay::createOverlayImpl);

    // Retry timer: if TouchInputHandler::start() fails at first show() (e.g.
    // the INGENIC device is still in its USB reset sequence), keep retrying
    // every 2 s until the device appears.  Stopped in hide() so a late retry
    // cannot reopen devices after teardown.
    m_touchRetryTimer = new QTimer(this);
    m_touchRetryTimer->setSingleShot(true);
    m_touchRetryTimer->setInterval(2000);
    connect(m_touchRetryTimer, &QTimer::timeout, this, [this]() {
        if (!m_active)
            return;
        if (!m_touch->start()) {
            qWarning() << "TouchpadOverlay: touch start retry failed, retrying in 2 s";
            m_touchRetryTimer->start();
        } else {
            qDebug() << "TouchpadOverlay: touch input started on retry";
        }
    });

    watchVirtualKeyboard();

    QDBusConnection::sessionBus().connect(
        QString(),
        QStringLiteral("/org/kde/plasma/touchpad"),
        QStringLiteral("org.kde.plasma.touchpad"),
        QStringLiteral("configChanged"),
        this, SLOT(reloadSettings()));

    // Follow eDP-2 coming and going (suspend/resume re-announces every
    // output).  Both handlers only inspect the screen and defer any window
    // work, so nothing is shown, hidden or deleted while Qt is still inside
    // its own output-change dispatch.
    connect(qGuiApp, &QGuiApplication::screenAdded,
            this, &TouchpadOverlay::onScreenAdded);
    connect(qGuiApp, &QGuiApplication::screenRemoved,
            this, &TouchpadOverlay::onScreenRemoved);
}

TouchpadOverlay::~TouchpadOverlay()
{
    hide();
    delete m_view;
    delete m_blackout;
}

void TouchpadOverlay::onScreenAdded(QScreen *screen)
{
    if (screen->name() != QLatin1String(BOTTOM_SCREEN))
        return;
    if (m_active || m_pendingPosition == DockPosition::Detached)
        return;
    // Give the compositor time to finish announcing all outputs before the
    // overlay is shown again.
    qDebug() << "TouchpadOverlay:" << BOTTOM_SCREEN << "added; showing overlay in 1.5 s";
    m_showTimer->start(1500);
}

void TouchpadOverlay::onScreenRemoved(QScreen *screen)
{
    if (screen->name() != QLatin1String(BOTTOM_SCREEN) || !m_active)
        return;
    // The QScreen is about to be deleted; Qt moves our (still valid) windows
    // elsewhere on its own.  Hide them once Qt has finished, keeping the
    // requested position so onScreenAdded() can bring the overlay back.
    qDebug() << "TouchpadOverlay:" << BOTTOM_SCREEN << "removed; hiding overlay";
    QMetaObject::invokeMethod(this, [this]() {
        deactivate();
        // eDP-2 may already be back (onScreenAdded() ignored it while the
        // overlay was still active); if so, schedule the re-show here.
        if (m_pendingPosition != DockPosition::Detached && findBottomScreen())
            m_showTimer->start(1500);
    }, Qt::QueuedConnection);
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

bool TouchpadOverlay::isInTentMode() const
{
    QScreen *edp1 = nullptr;
    QScreen *edp2 = nullptr;
    for (QScreen *s : QGuiApplication::screens()) {
        if (s->name() == QStringLiteral("eDP-1"))
            edp1 = s;
        else if (s->name() == QLatin1String(BOTTOM_SCREEN))
            edp2 = s;
    }
    return edp1 && edp2 && edp1->geometry().topLeft() == edp2->geometry().topLeft();
}

void TouchpadOverlay::show(DockPosition position)
{
    // Hide any visible overlay before doing anything with screens, so no
    // layer surface is mapped while kscreen-doctor reconfigures outputs.  This
    // is also how a position change is applied: hide, then show with the new
    // anchors and size.
    m_showTimer->stop();
    deactivate();

    m_pendingPosition = position;
    if (position == DockPosition::Detached)
        return;

    if (isInTentMode()) {
        // In tent mode both screens share the same origin; force clamshell
        // layout without blocking.  Defer showing the overlay so the Wayland
        // output events from kscreen-doctor arrive while it is hidden.
        qDebug() << "TouchpadOverlay: screens overlap (tent mode); forcing clamshell rotation";
        QProcess::execute(QStringLiteral("kscreen-doctor"), {
            QStringLiteral("output.eDP-1.rotation.inverted"),
            QStringLiteral("output.eDP-1.position.0,0"),
            QStringLiteral("output.eDP-2.rotation.none"),
            QStringLiteral("output.eDP-2.position.0,1125")
        });
        m_showTimer->start(400);
        return;
    }

    createOverlayImpl();
}

void TouchpadOverlay::ensureWindows()
{
    if (m_view)
        return;

    // Full-screen black layer on eDP-2, sits behind the touchpad overlay.
    // LayerTop (2) is below LayerOverlay (3), so the touchpad renders on top.
    m_blackout = new QQuickView();
    m_blackout->setResizeMode(QQuickView::SizeRootObjectToView);
    m_blackout->setSource(QUrl(QStringLiteral("qrc:/org/kde/plasma/touchpad/qml/Blackout.qml")));
    m_blackout->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    m_blackoutLayerWindow = LayerShellQt::Window::get(m_blackout);
    m_blackoutLayerWindow->setLayer(LayerShellQt::Window::LayerTop);
    m_blackoutLayerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_blackoutLayerWindow->setScope(QStringLiteral("plasma-touchpad-blackout"));
    m_blackoutLayerWindow->setExclusiveZone(-1);
    // If the compositor dismisses the surface (e.g. its output goes away),
    // don't let LayerShellQt close() the window; onScreenRemoved() hides it.
    m_blackoutLayerWindow->setCloseOnDismissed(false);
    m_blackoutLayerWindow->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight |
        LayerShellQt::Window::AnchorTop |
        LayerShellQt::Window::AnchorBottom));
    m_blackoutLayerWindow->setDesiredSize(QSize(0, 0)); // compositor fills all-anchored surface

    connect(m_blackout, &QWindow::screenChanged, this, [this](QScreen *s) {
        if (!m_active || !s || s->name() == QLatin1String(BOTTOM_SCREEN))
            return;
        if (QScreen *screen = findBottomScreen())
            m_blackoutLayerWindow->setScreen(screen);
    });

    // Create the QML overlay window.
    m_view = new QQuickView();
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setSource(QUrl(QStringLiteral("qrc:/org/kde/plasma/touchpad/qml/Touchpad.qml")));
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);

    // Configure as a LayerShell overlay surface.  Screen, anchors and size
    // depend on the dock position and are set in createOverlayImpl().
    m_layerWindow = LayerShellQt::Window::get(m_view);
    m_layerWindow->setLayer(LayerShellQt::Window::LayerOverlay);
    m_layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    m_layerWindow->setScope(QStringLiteral("plasma-touchpad"));
    m_layerWindow->setExclusiveZone(-1); // don't push other surfaces away
    m_layerWindow->setCloseOnDismissed(false);

    connect(m_view, &QWindow::screenChanged, this, [this](QScreen *s) {
        const QString name = s ? s->name() : QStringLiteral("(null)");
        qDebug() << "TouchpadOverlay: overlay screen changed to" << name;
        if (!m_active || !s || s->name() == QLatin1String(BOTTOM_SCREEN))
            return;
        // KWin moved the overlay off eDP-2 (probably because it reassigned the
        // active screen after windows were moved).  Re-pin by calling setScreen
        // again — this triggers LayerShellQt to recreate the surface on eDP-2.
        qDebug() << "TouchpadOverlay: re-pinning overlay back to" << BOTTOM_SCREEN;
        if (QScreen *screen = findBottomScreen()) {
            m_layerWindow->setScreen(screen);
            m_view->show();
            return;
        }
        qWarning() << "TouchpadOverlay: eDP-2 not found in screen list, cannot re-pin";
    });
}

void TouchpadOverlay::createOverlayImpl()
{
    if (m_active || m_pendingPosition == DockPosition::Detached)
        return;

    QScreen *targetScreen = findBottomScreen();
    if (!targetScreen) {
        // onScreenAdded() shows the overlay once eDP-2 appears.
        qWarning() << "TouchpadOverlay: screen" << BOTTOM_SCREEN << "not found";
        return;
    }
    m_screenW = static_cast<int>(targetScreen->geometry().width());
    m_screenH = static_cast<int>(targetScreen->geometry().height());

    const Settings s = loadFromConfig();

    // Move windows and panels to eDP-1 BEFORE creating the overlay surface.
    // This lets KWin complete its active-screen reassignment now, so it has
    // no reason to reassign the overlay once it appears on eDP-2.
    m_windows->moveWindowsToTopScreen();
    m_panels->movePanelsToTopScreen();
    setVirtualKeyboardEnabled(false);
    // Suppress the rotation script before any Wayland surface is created.
    // kscreen-doctor (called by rotate-screens.sh) causes the compositor to
    // send wl_output.transform events to alive surfaces; if one arrives while
    // a QWaylandWindow is alive, QWaylandWindow::updateBufferTransform() calls
    // QWindow::screen() which crashes when the screen is transiently null
    // during output reconfiguration.  Suppression is lifted in deactivate().
    setRotationSuppressed(true);

    ensureWindows();

    m_blackout->setScreen(targetScreen);
    m_blackoutLayerWindow->setScreen(targetScreen);
    m_blackout->show();

    // Pin Qt's own screen tracking to eDP-2 before the layer surface is
    // created.  LayerShellQt falls back to QWindow::screen() when its own
    // screen property is not set, so both must agree to ensure the overlay
    // lands on eDP-2 regardless of which screen is currently active/primary.
    m_view->setScreen(targetScreen);

    // Explicitly pin the LayerShell surface to eDP-2.  setScreen() stores the
    // QScreen* in LayerShellQt::WindowPrivate and uses it directly when the
    // zwlr_layer_surface_v1 is created at show() time, avoiding the "placeholder
    // screen" path that QWindow::screen() can fall into before the compositor
    // has fully mapped the output.
    m_layerWindow->setScreen(targetScreen);

    // TopHalf dock  → overlay on bottom half → anchor bottom
    // BottomHalf dock → overlay on top half  → anchor top
    const bool overlayOnTop = (m_pendingPosition == DockPosition::BottomHalf);
    LayerShellQt::Window::Anchor vertAnchor = overlayOnTop
        ? LayerShellQt::Window::AnchorTop
        : LayerShellQt::Window::AnchorBottom;
    m_layerWindow->setAnchors(LayerShellQt::Window::Anchors(
        LayerShellQt::Window::AnchorLeft |
        LayerShellQt::Window::AnchorRight |
        vertAnchor));
    const int trim = (m_pendingPosition == DockPosition::BottomHalf) ? OVERLAY_TRIM_BOTTOM_HALF
                                                                     : OVERLAY_TRIM_TOP_HALF;
    m_layerWindow->setDesiredSize(QSize(0, m_screenH / 2 - trim));

    m_view->show();
    m_active = true;

    const QRectF touchRect = overlayRect(m_pendingPosition);
    m_touch->setScreenSize(m_screenW, m_screenH);
    m_touch->setTouchpadRect(touchRect);
    applySettings(s);
    if (!m_touch->start()) {
        qWarning() << "TouchpadOverlay: touch input start failed (device not ready?), retrying in 2 s";
        m_touchRetryTimer->start();
    }

    qDebug() << "TouchpadOverlay: shown on" << BOTTOM_SCREEN
             << "position" << static_cast<int>(m_pendingPosition)
             << "touchRect" << touchRect;
}

void TouchpadOverlay::applySettings(const Settings &s)
{
    m_touch->setButtonCount(s.buttonCount);
    if (m_view && m_view->rootObject()) {
        m_view->rootObject()->setProperty("buttonCount", s.buttonCount);
        m_view->rootObject()->setProperty("colorScheme", s.colorScheme);
    }
    m_touch->setPointerSpeed(s.pointerSpeed);
    m_touch->setPointerAcceleration(s.pointerAcceleration);
    m_touch->setScrollSpeed(s.scrollSpeed);
    m_touch->setThreeFingerSwipe(s.threeFingerSwipe);
    m_touch->setFourFingerShowDesktop(s.fourFingerShowDesktop);
    m_touch->setPinchZoom(s.pinchZoom);
    m_touch->setTapToClick(s.tapToClick);
    m_touch->setTwoFingerRightClick(s.twoFingerRightClick);
    m_touch->setTwoFingerRotate(s.twoFingerRotate);
    m_touch->setInvertScroll(s.invertScroll);
    // Always suppress rotation while the overlay is visible, regardless of
    // the disableRotation setting.  The rotation script calls kscreen-doctor
    // which triggers wl_output.transform events that crash updateBufferTransform
    // when any QWaylandWindow is alive.  Rotation is re-enabled in deactivate().
    setRotationSuppressed(m_active);
}

void TouchpadOverlay::reloadSettings()
{
    if (!m_active)
        return;
    applySettings(loadFromConfig());
    qDebug() << "TouchpadOverlay: settings reloaded";
}


void TouchpadOverlay::hide()
{
    m_pendingPosition = DockPosition::Detached;
    m_showTimer->stop();
    deactivate();
}

void TouchpadOverlay::deactivate()
{
    if (!m_active)
        return;
    m_active = false;

    m_touchRetryTimer->stop();
    m_touch->stop();
    m_touch->setTouchpadRect(QRectF());
    m_windows->restore();
    m_panels->restorePanels();
    setVirtualKeyboardEnabled(true);
    setRotationSuppressed(false);

    // Hide, never delete: the views are reused by the next show().
    m_blackout->hide();
    m_view->hide();

    qDebug() << "TouchpadOverlay: hidden";
}

void TouchpadOverlay::setRotationSuppressed(bool suppress)
{
    const QString signal = suppress ? QStringLiteral("-STOP") : QStringLiteral("-CONT");
    QProcess::startDetached(QStringLiteral("pkill"), {signal, QStringLiteral("-f"), QStringLiteral("rotate-screens.sh")});
    qDebug() << "TouchpadOverlay: rotation script" << (suppress ? "paused" : "resumed");
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
    if (!m_active)
        return;  // overlay not visible — don't interfere

    const auto it = changed.find(QStringLiteral("enabled"));
    if (it != changed.end() && it->toBool()) {
        qDebug() << "TouchpadOverlay: suppressing OSK re-enable while touchpad is active";
        setVirtualKeyboardEnabled(false);
    }
}

} // namespace PlasmaTP
