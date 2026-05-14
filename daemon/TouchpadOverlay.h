#pragma once

#include "DockWatcher.h"

#include <QObject>
#include <QRectF>

namespace LayerShellQt { class Window; }
class QQuickView;

namespace PlasmaTP {

class TouchInputHandler;
class WindowMover;
class PanelMover;

/*
 * TouchpadOverlay manages the full lifecycle of the on-screen touchpad:
 *
 *   show(position) — creates a LayerShell window anchored to eDP-2,
 *                    covering the half of the screen not occupied by the
 *                    keyboard.  Moves windows and panels to the top screen.
 *                    Starts TouchInputHandler.
 *
 *   hide()         — destroys the overlay window, restores windows/panels,
 *                    stops TouchInputHandler.
 *
 * The overlay is a QQuickView loaded from qml/Touchpad.qml.
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

    bool isVisible() const { return m_view != nullptr; }

private Q_SLOTS:
    void onVirtualKeyboardPropertyChanged(const QString &iface,
                                          const QVariantMap &changed,
                                          const QStringList &invalidated);

private:
    QRectF overlayRect(DockPosition position) const;
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
};

} // namespace PlasmaTP
