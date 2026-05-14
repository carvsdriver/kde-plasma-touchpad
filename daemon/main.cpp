#include "DockWatcher.h"
#include "TouchpadOverlay.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("plasma-touchpad"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationDomain(QStringLiteral("kde.org"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Yoga Book 9 on-screen touchpad daemon"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    PlasmaTP::DockWatcher watcher;
    PlasmaTP::TouchpadOverlay overlay;

    QObject::connect(&watcher, &PlasmaTP::DockWatcher::dockChanged,
                     [&overlay](PlasmaTP::DockPosition pos) {
        if (pos == PlasmaTP::DockPosition::Detached)
            overlay.hide();
        else
            overlay.show(pos);
    });

    // Apply initial state (keyboard may already be docked at startup)
    if (watcher.isDocked())
        overlay.show(watcher.position());

    return app.exec();
}
