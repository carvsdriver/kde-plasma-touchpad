#pragma once

#include <QObject>
#include <QSize>
#include <QStringList>

namespace PlasmaTP {

/*
 * DashboardManager fills the area above the keyboard on the bottom screen
 * while dashboard mode is shown, and clears it again afterwards: YouTube,
 * Weather Report and news, in three equal columns.
 *
 * YouTube and news are this project's own widgets
 * (org.kde.plasma.touchpad.youtube / .news; the stock Web Browser widget is
 * the fallback when they are not installed).  They show items themselves, or
 * hand them to the default browser.
 *
 * Plasma can't hide a desktop widget, so leaving dashboard mode removes them
 * after saving each one's settings (weather location, etc.) to
 * plasma-touchpadrc [Dashboard].  Showing it again re-adds them with those
 * settings written into Plasma's config *before* the widget is created: a Web
 * Browser widget reads its page at load time and ignores configuration
 * written afterwards.  Each widget is tagged in its [General] config with
 * plasmaTouchpadDashboard=<role>.
 *
 * While they are shown the bottom desktop's wallpaper is swapped for a plain
 * fill in the colour scheme's window colour.
 *
 * Screen index 0 is eDP-2 (see PanelMover).
 */
class DashboardManager : public QObject
{
    Q_OBJECT

public:
    explicit DashboardManager(QObject *parent = nullptr);

    // Add the dashboard widgets, laid out across the top `area` (logical
    // pixels) of the bottom screen, replacing any left from before.
    void showWidgets(const QSize &area);
    // Save the dashboard widgets' settings, then remove them.
    void hideWidgets();

private:
    QStringList desktopConfigKeys() const;
    // Replace the bottom desktop's wallpaper with a plain fill in the colour
    // scheme's window colour, so no wallpaper shows around or through the
    // widgets; restore the previous wallpaper plugin afterwards.
    void showBackdrop();
    void hideBackdrop();
    // Save the dashboard widgets' settings, then remove them.
    void removeWidgets();
    // Widget plugin `id` if installed, else `fallback`.
    static QString widgetType(const char *id, const char *fallback);
};

} // namespace PlasmaTP
