#include "DashboardManager.h"
#include "PlasmaScript.h"
#include "Settings.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QRegularExpression>
#include <QStandardPaths>

namespace PlasmaTP {

static constexpr const char *YOUTUBE_URL = "https://m.youtube.com/";
static constexpr const char *NEWS_URL = "https://text.npr.org/";

// plasma-touchpadrc group and key holding the saved widget settings, as JSON:
// { role: { "type": plugin, "entries": [ { "path": [group, ...], "key": k, "value": v }, ... ] }, ... }
static constexpr const char *SAVED_GROUP = "Dashboard";
static constexpr const char *SAVED_KEY = "widgetSettings";
// The wallpaper plugin the bottom desktop had before the dashboard backdrop
// replaced it.  Kept in the config file so a crash can't lose it.
static constexpr const char *SAVED_WALLPAPER_KEY = "previousWallpaperPlugin";
static constexpr const char *BACKDROP_PLUGIN = "org.kde.color";

DashboardManager::DashboardManager(QObject *parent)
    : QObject(parent)
{}

// Setting names of the desktop containment itself.  Writing one of these
// through the containment's script object changes the desktop's own setting
// (e.g. "url" is the folder it shows), whatever group is selected, so they
// are never pre-seeded.
QStringList DashboardManager::desktopConfigKeys() const
{
    const QString path = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/plasmoids/org.kde.desktopcontainment/contents/config/main.xml"));
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        qWarning() << "DashboardManager: desktop containment config schema not found";
        return {};
    }
    static const QRegularExpression entry(QStringLiteral(R"re(<entry\s+name="([^"]+)")re"));
    QStringList keys;
    auto it = entry.globalMatch(QString::fromUtf8(file.readAll()));
    while (it.hasNext())
        keys.append(it.next().captured(1));
    return keys;
}

QString DashboardManager::widgetType(const char *id, const char *fallback)
{
    const QString metadata = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasma/plasmoids/%1/metadata.json").arg(QLatin1String(id)));
    return QLatin1String(metadata.isEmpty() ? fallback : id);
}

void DashboardManager::showBackdrop()
{
    const QString color = qGuiApp->palette().color(QPalette::Active, QPalette::Window)
                              .name(QColor::HexRgb);
    const QString script = QStringLiteral(R"JS(
        var desktop = desktops().filter(function (d) { return d.screen === 0; })[0];
        if (!desktop)
            throw "no desktop containment on screen 0";
        print(desktop.wallpaperPlugin);
        desktop.wallpaperPlugin = "%1";
        desktop.currentConfigGroup = ["Wallpaper", "%1", "General"];
        desktop.writeConfig("Color", "%2");
        desktop.reloadConfig();
    )JS").arg(QLatin1String(BACKDROP_PLUGIN), color);

    const QString previous = runPlasmaScript(script).trimmed();
    if (previous.isEmpty())
        return;

    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(SAVED_GROUP));
    // Already set means the backdrop was never taken down (crash); the saved
    // plugin is still the user's real one.
    if (!grp.hasKey(SAVED_WALLPAPER_KEY) && previous != QLatin1String(BACKDROP_PLUGIN)) {
        grp.writeEntry(SAVED_WALLPAPER_KEY, previous);
        cfg.sync();
    }
    qDebug() << "DashboardManager: backdrop" << color << "replaces wallpaper" << previous;
}

void DashboardManager::hideBackdrop()
{
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(SAVED_GROUP));
    const QString previous = grp.readEntry(SAVED_WALLPAPER_KEY, QString());
    if (previous.isEmpty())
        return;

    const QString script = QStringLiteral(R"JS(
        var desktop = desktops().filter(function (d) { return d.screen === 0; })[0];
        if (!desktop)
            throw "no desktop containment on screen 0";
        desktop.wallpaperPlugin = "%1";
        print("ok");
    )JS").arg(previous);

    if (runPlasmaScript(script).trimmed() != QLatin1String("ok")) {
        qWarning() << "DashboardManager: could not restore wallpaper" << previous;
        return;
    }
    grp.deleteEntry(SAVED_WALLPAPER_KEY);
    cfg.sync();
    qDebug() << "DashboardManager: restored wallpaper" << previous;
}

void DashboardManager::showWidgets(const QSize &area)
{
    showBackdrop();
    // Widgets left from a session that ended without hiding the dashboard
    // (a crash or a freeze) come back laid out by Plasma, not by us, and a
    // script can't move them.  Take them down, settings saved, and start
    // afresh.
    removeWidgets();

    // Widgets, by column.
    QJsonArray roles;
    auto role = [&roles](const char *name, const QString &type, int column, const char *url) {
        QJsonObject r{{QStringLiteral("role"), QLatin1String(name)},
                      {QStringLiteral("type"), type},
                      {QStringLiteral("column"), column}};
        // Only the stock Web Browser widget takes a starting page.
        if (url && type == QLatin1String("org.kde.plasma.webbrowser"))
            r.insert(QStringLiteral("url"), QLatin1String(url));
        roles.append(r);
    };
    role("youtube", widgetType("org.kde.plasma.touchpad.youtube", "org.kde.plasma.webbrowser"), 0, YOUTUBE_URL);
    role("weather", QStringLiteral("org.kde.plasma.weather"), 1, nullptr);
    role("news", widgetType("org.kde.plasma.touchpad.news", "org.kde.plasma.webbrowser"), 2, NEWS_URL);

    KConfig cfg(QLatin1String(Settings::configFile()));
    const QByteArray saved = cfg.group(QLatin1String(SAVED_GROUP))
                                 .readEntry(SAVED_KEY, QByteArray("{}"));
    const QStringList skip = desktopConfigKeys();
    // null tells the script not to pre-seed at all.
    const QString skipJson = skip.isEmpty()
        ? QStringLiteral("null")
        : QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(skip)).toJson(QJsonDocument::Compact));

    // A widget's settings are written under the ID it is about to get,
    // before it is created.  IDs are handed out in sequence, so a throwaway
    // widget reveals the next one.  New browser widgets open their page via
    // useDefaultUrl/defaultUrl; "url" would also be the desktop's own key.
    const QString script = QStringLiteral(R"JS(
        var AREA_WIDTH = %1;
        var AREA_HEIGHT = %2;
        var SAVED = %3;
        var SKIP = %4;
        var ROLES = %5;

        var desktop = desktops().filter(function (d) { return d.screen === 0; })[0];
        if (!desktop)
            throw "no desktop containment on screen 0";

        var wanted = {};
        ROLES.forEach(function (r) { wanted[r.role] = r.type; });
        var present = {};
        desktop.widgets().forEach(function (w) {
            w.currentConfigGroup = ["General"];
            var role = w.readConfig("plasmaTouchpadDashboard", "");
            if (!role)
                return;
            // e.g. a Web Browser widget left from before the dashboard's own
            // widgets were installed.
            if (wanted[role] !== w.type)
                w.remove();
            else
                present[role] = true;
        });

        var columnWidth = Math.floor(AREA_WIDTH / 3);
        ROLES.forEach(function (r) {
            var column = r.column;
            if (present[r.role])
                return;

            var settings = [{ path: ["General"], key: "plasmaTouchpadDashboard", value: r.role }];
            if (r.url) {
                settings.push({ path: ["General"], key: "useDefaultUrl", value: "true" });
                settings.push({ path: ["General"], key: "defaultUrl", value: r.url });
            }
            // Saved settings come last so they win over the defaults.  They
            // only apply to a widget of the type they were saved from (older
            // saves are a bare list, with no type).
            var saved = SAVED[r.role];
            if (saved && !Array.isArray(saved))
                saved = saved.type === r.type ? saved.entries : [];
            settings = settings.concat(saved || []);

            var nextId = -1;
            if (SKIP) {
                var probe = desktop.addWidget("org.kde.plasma.panelspacer",
                                              AREA_WIDTH - 60, AREA_HEIGHT + 100, 50, 50);
                nextId = probe.id + 1;
                probe.remove();
                settings.forEach(function (s) {
                    if (SKIP.indexOf(s.key) >= 0)
                        return;
                    desktop.currentConfigGroup =
                        ["Applets", String(nextId), "Configuration"].concat(s.path);
                    desktop.writeConfig(s.key, s.value);
                });
            }

            // addWidget treats a position of exactly (0,0) as "none given"
            // and centres the widget, so keep the first column off the origin.
            var x = Math.max(column * columnWidth, 1);
            var w = desktop.addWidget(r.type, x, 0, columnWidth, AREA_HEIGHT);
            if (w.id !== nextId) {
                // Not pre-seeded: apply what we can now.  A browser keeps its
                // built-in page until Plasma restarts.
                settings.forEach(function (s) {
                    w.currentConfigGroup = s.path;
                    w.writeConfig(s.key, s.value);
                });
                w.reloadConfig();
                print(r.role + "(not pre-seeded) ");
            } else {
                print(r.role + " ");
            }
        });
    )JS").arg(QString::number(area.width()),
              QString::number(area.height()),
              QString::fromUtf8(saved),
              skipJson,
              QString::fromUtf8(QJsonDocument(roles).toJson(QJsonDocument::Compact)));

    const QString added = runPlasmaScript(script).trimmed();
    if (added.isEmpty())
        qDebug() << "DashboardManager: widgets already present";
    else
        qDebug() << "DashboardManager: added widgets:" << added;
}

void DashboardManager::hideWidgets()
{
    hideBackdrop();
    removeWidgets();
}

void DashboardManager::removeWidgets()
{
    const QString script = QStringLiteral(R"JS(
        var saved = {};
        desktops().forEach(function (d) {
            d.widgets().forEach(function (w) {
                w.currentConfigGroup = ["General"];
                var role = w.readConfig("plasmaTouchpadDashboard", "");
                if (!role)
                    return;
                var entries = [];
                (function collect(path) {
                    w.currentConfigGroup = path;
                    w.configKeys.forEach(function (k) {
                        entries.push({ path: path, key: k, value: String(w.readConfig(k, "")) });
                    });
                    // configGroups is a live view of the current group, which
                    // the recursion changes; copy it before descending.
                    Array.prototype.slice.call(w.configGroups).forEach(function (g) {
                        collect(path.concat([g]));
                    });
                })([]);
                saved[role] = { type: w.type, entries: entries };
                w.remove();
            });
        });
        print(JSON.stringify(saved));
    )JS");

    const QJsonDocument doc = QJsonDocument::fromJson(runPlasmaScript(script).trimmed().toUtf8());
    if (!doc.isObject()) {
        qWarning() << "DashboardManager: could not save widget settings";
        return;
    }
    const QJsonObject removed = doc.object();
    if (removed.isEmpty())
        return;

    // Merge, so a role the user deleted keeps its last saved settings.
    KConfig cfg(QLatin1String(Settings::configFile()));
    KConfigGroup grp = cfg.group(QLatin1String(SAVED_GROUP));
    QJsonObject all = QJsonDocument::fromJson(grp.readEntry(SAVED_KEY, QByteArray("{}"))).object();
    for (auto it = removed.begin(); it != removed.end(); ++it)
        all.insert(it.key(), it.value());
    grp.writeEntry(SAVED_KEY, QJsonDocument(all).toJson(QJsonDocument::Compact));
    cfg.sync();
    qDebug() << "DashboardManager: saved and removed widgets:" << removed.keys();
}

} // namespace PlasmaTP
