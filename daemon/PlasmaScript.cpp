#include "PlasmaScript.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

namespace PlasmaTP {

QString runPlasmaScript(const QString &script)
{
    QDBusInterface plasma(QStringLiteral("org.kde.plasmashell"),
                          QStringLiteral("/PlasmaShell"),
                          QStringLiteral("org.kde.PlasmaShell"),
                          QDBusConnection::sessionBus());

    if (!plasma.isValid()) {
        qWarning() << "PlasmaScript: plasmashell DBus interface not available";
        return {};
    }

    QDBusReply<QString> reply = plasma.call(QStringLiteral("evaluateScript"), script);
    if (!reply.isValid()) {
        qWarning() << "PlasmaScript: evaluateScript failed:" << reply.error().message();
        return {};
    }
    return reply.value();
}

} // namespace PlasmaTP
