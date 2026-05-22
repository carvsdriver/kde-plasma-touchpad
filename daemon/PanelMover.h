#pragma once

#include <QObject>
#include <QList>

namespace PlasmaTP {

/*
 * PanelMover moves KDE Plasma panels off the bottom screen (eDP-2) to the top
 * screen (eDP-1) when the touchpad overlay activates, and restores them on
 * deactivation.
 *
 * Plasma panels are identified by their containment ID and screen index.
 * We use the plasmashell DBus scripting interface to enumerate and relocate them.
 *
 * Screen index mapping (confirmed via screenGeometry() at runtime):
 *   Screen 0 = eDP-2 (bottom screen, y=1125)
 *   Screen 1 = eDP-1 (top screen,    y=0)
 */
class PanelMover : public QObject
{
    Q_OBJECT

public:
    explicit PanelMover(QObject *parent = nullptr);

    // Move all panels on eDP-2 to eDP-1. Records which panels were moved.
    void movePanelsToTopScreen();
    // Restore moved panels back to eDP-2.
    void restorePanels();

private:
    QString runPlasmaScript(const QString &script);

    // IDs of panels that were moved (Plasma containment IDs)
    QList<int> m_movedPanelIds;
    bool m_moved = false;
};

} // namespace PlasmaTP
