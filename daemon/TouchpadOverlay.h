#pragma once

#include "DockWatcher.h"
#include "../common/Settings.h"

#include <QObject>
#include <QRectF>
#include <QTimer>

namespace LayerShellQt { class Window; }
class QQuickView;
class QScreen;

namespace PlasmaTP {

class TouchInputHandler;
class WindowMover;
class PanelMover;

/*
 * TouchpadOverlay manages the full lifecycle of the on-screen touchpad:
 *
 *   show(position) — shows a LayerShell window anchored to eDP-2,
 *                    covering the half of the screen not occupied by the
 *                    keyboard.  Moves windows and panels to the top screen.
 *                    Starts TouchInputHandler.
 *
 *   hide()         — hides the overlay window, restores windows/panels,
 *                    stops TouchInputHandler.
 *
 * The overlay is a QQuickView loaded from qml/Touchpad.qml.  The overlay and
 * blackout views are created once and only shown/hidden afterwards: deleting
 * a QWindow while Qt is reshuffling Wayland outputs (suspend/resume, rotation)
 * can leave an orphaned QWaylandWindow behind that later dereferences the
 * freed QWindow and crashes.  Hiding keeps the QWindow alive and drops the
 * layer surface; showing again creates a fresh layer surface.
 *
 * Screen geometry assumptions (2880×1800 per screen):
 *   TopHalf    dock → overlay occupies bottom half: y=900, h=900
 *   BottomHalf dock → overlay occupies top half:    y=0,   h=900
 */
class TouchpadOverlay : public QObject
{
    Q_OBJECT

public:
    explicit TouchpadOverlay(QObject *parent = nullptr);
    ~TouchpadOverlay() override;

    void show(DockPosition position);
    void hide();

private Q_SLOTS:
    void reloadSettings();
    void onVirtualKeyboardPropertyChanged(const QString &iface,
                                          const QVariantMap &changed,
                                          const QStringList &invalidated);

private:
    QRectF overlayRect(DockPosition position) const;
    void applySettings(const Settings &s);
    bool isInTentMode() const;
    void ensureWindows();
    void createOverlayImpl();
    void deactivate();
    void onScreenAdded(QScreen *screen);
    void onScreenRemoved(QScreen *screen);
    void setRotationSuppressed(bool suppress);
    void setVirtualKeyboardEnabled(bool enabled);
    void watchVirtualKeyboard();

    QQuickView *m_blackout = nullptr;
    LayerShellQt::Window *m_blackoutLayerWindow = nullptr;
    QQuickView *m_view = nullptr;
    LayerShellQt::Window *m_layerWindow = nullptr;
    TouchInputHandler *m_touch = nullptr;
    WindowMover *m_windows = nullptr;
    PanelMover *m_panels = nullptr;

    // Bottom screen resolution — read at first show() from QScreen
    int m_screenW = 2880;
    int m_screenH = 1800;

    QTimer *m_buttonHighlightTimer = nullptr;
    QTimer *m_showTimer = nullptr;
    QTimer *m_touchRetryTimer = nullptr;
    // Requested dock position; Detached once hide() is called.  Kept across
    // internal deactivations (eDP-2 going away) so the overlay can come back.
    DockPosition m_pendingPosition = DockPosition::Detached;
    // True while the overlay is shown and touch input is running.
    bool m_active = false;
};

} // namespace PlasmaTP
