#include "PanelMover.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

namespace PlasmaTP {

static constexpr const char *PLASMA_SERVICE = "org.kde.plasmashell";
static constexpr const char *PLASMA_SHELL_PATH = "/PlasmaShell";
static constexpr const char *PLASMA_SHELL_IFACE = "org.kde.PlasmaShell";

PanelMover::PanelMover(QObject *parent)
    : QObject(parent)
{}

void PanelMover::movePanelsToTopScreen()
{
    if (m_moved)
        return;

    // Screen index mapping (confirmed via screenGeometry() — y=0 is the top
    // physical screen, y>0 is the bottom):
    //   Screen 0 = eDP-2 (bottom physical screen, y=1125)
    //   Screen 1 = eDP-1 (top physical screen,    y=0)
    //
    // We move panels from screen 0 (bottom) to screen 1 (top) and print each
    // moved panel's ID so we can restore precisely. evaluateScript captures
    // print() output as its return value.
    const QString script = QStringLiteral(R"JS(
        var ps = panels();
        for (var i = 0; i < ps.length; i++) {
            var p = ps[i];
            if (p.screen === 0) {
                p.screen = 1;
                print(p.id);
            }
        }
    )JS");

    const QString result = runPlasmaScript(script).trimmed();
    m_movedPanelIds.clear();
    if (!result.isEmpty()) {
        const QStringList parts = result.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            bool ok = false;
            int id = part.trimmed().toInt(&ok);
            if (ok)
                m_movedPanelIds.append(id);
        }
    }
    qDebug() << "PanelMover: moved panels" << m_movedPanelIds << "to top screen";
    m_moved = true;
}

void PanelMover::restorePanels()
{
    if (!m_moved)
        return;

    if (m_movedPanelIds.isEmpty()) {
        m_moved = false;
        return;
    }

    // Build a JS array literal of the IDs we recorded, then move only those
    // panels from screen 1 (top) back to screen 0 (bottom).
    QStringList idStrs;
    for (int id : m_movedPanelIds)
        idStrs.append(QString::number(id));

    const QString script = QStringLiteral(R"JS(
        var ids = [%1];
        var ps = panels();
        for (var i = 0; i < ps.length; i++) {
            var p = ps[i];
            for (var j = 0; j < ids.length; j++) {
                if (p.id === ids[j]) {
                    p.screen = 0;
                    print("restored panel " + p.id);
                }
            }
        }
    )JS").arg(idStrs.join(QLatin1Char(',')));

    const QString result = runPlasmaScript(script);
    qDebug() << "PanelMover: restore result:" << result.trimmed();
    m_movedPanelIds.clear();
    m_moved = false;
}

QString PanelMover::runPlasmaScript(const QString &script)
{
    QDBusInterface plasma(QLatin1String(PLASMA_SERVICE),
                          QLatin1String(PLASMA_SHELL_PATH),
                          QLatin1String(PLASMA_SHELL_IFACE),
                          QDBusConnection::sessionBus());

    if (!plasma.isValid()) {
        qWarning() << "PanelMover: plasmashell DBus interface not available";
        return {};
    }

    QDBusReply<QString> reply = plasma.call(QStringLiteral("evaluateScript"), script);
    if (!reply.isValid()) {
        qWarning() << "PanelMover: evaluateScript failed:" << reply.error().message();
        return {};
    }
    return reply.value();
}

} // namespace PlasmaTP
