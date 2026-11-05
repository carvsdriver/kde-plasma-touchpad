#include "KWinScript.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

namespace PlasmaTP {

static constexpr const char *KWIN_SERVICE = "org.kde.KWin";
static constexpr const char *KWIN_SCRIPTING_PATH = "/Scripting";
static constexpr const char *KWIN_SCRIPTING_IFACE = "org.kde.kwin.Scripting";
static constexpr const char *KWIN_SCRIPT_IFACE = "org.kde.kwin.Script";

static QDBusInterface scriptingInterface()
{
    return QDBusInterface(QLatin1String(KWIN_SERVICE), QLatin1String(KWIN_SCRIPTING_PATH),
                          QLatin1String(KWIN_SCRIPTING_IFACE), QDBusConnection::sessionBus());
}

void unloadKWinScript(const QString &name)
{
    QDBusInterface kwin = scriptingInterface();
    if (kwin.isValid())
        kwin.call(QStringLiteral("unloadScript"), name);
}

bool runKWinScript(const QString &name, const QString &script)
{
    // loadScript takes a file path, not the script itself.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    const QString path = dir + QLatin1Char('/') + name + QStringLiteral(".js");
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning() << "KWinScript: cannot write" << path;
            return false;
        }
        f.write(script.toUtf8());
    }

    QDBusInterface kwin = scriptingInterface();
    if (!kwin.isValid()) {
        qWarning() << "KWinScript: KWin scripting DBus not available";
        return false;
    }

    // loadScript refuses (returns -1) while a script of the same name is
    // loaded, so unload first - and wait for KWin to finish: unloading is
    // asynchronous, and loading under a name still being unloaded fails.
    kwin.call(QStringLiteral("unloadScript"), name);
    for (int i = 0; i < 50; ++i) {
        QDBusReply<bool> loaded = kwin.call(QStringLiteral("isScriptLoaded"), name);
        if (!loaded.isValid() || !loaded.value())
            break;
        QThread::msleep(20);
    }
    QDBusReply<int> id = kwin.call(QStringLiteral("loadScript"), path, name);
    if (!id.isValid() || id.value() < 0) {
        qWarning() << "KWinScript: loadScript failed for" << name << id.error().message();
        return false;
    }

    QDBusInterface scriptIface(QLatin1String(KWIN_SERVICE),
                               QStringLiteral("/Scripting/Script") + QString::number(id.value()),
                               QLatin1String(KWIN_SCRIPT_IFACE), QDBusConnection::sessionBus());
    scriptIface.call(QStringLiteral("run"));
    return true;
}

} // namespace PlasmaTP
