#include "DockWatcher.h"
#include "TouchpadOverlay.h"
#include "YouTubeAccount.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

// Termination signals (systemctl stop, logout, Ctrl+C) are turned into a
// normal event-loop exit so the destructors run: they remove the dashboard
// widgets and move windows and panels back.  A signal handler can only do
// async-signal-safe work, so it writes a byte to a socket that the event
// loop watches.
static int s_signalFd[2] = {-1, -1};

static void onTerminationSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] ssize_t n = ::write(s_signalFd[0], &byte, 1);
}

static void installTerminationHandler(QGuiApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s_signalFd) != 0) {
        qWarning() << "plasma-touchpad: socketpair failed; no clean shutdown on signals";
        return;
    }
    auto *notifier = new QSocketNotifier(s_signalFd[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier, &app]() {
        notifier->setEnabled(false);
        char byte;
        [[maybe_unused]] ssize_t n = ::read(s_signalFd[1], &byte, 1);
        qDebug() << "plasma-touchpad: termination signal received; cleaning up";
        app.quit();
    });

    struct sigaction sa = {};
    sa.sa_handler = onTerminationSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (int sig : {SIGTERM, SIGINT, SIGHUP})
        ::sigaction(sig, &sa, nullptr);
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("plasma-touchpad"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationDomain(QStringLiteral("kde.org"));
    // The overlay windows are hidden and shown, never closed for good; the
    // daemon must keep running with no visible window.
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Yoga Book 9 on-screen touchpad daemon"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    installTerminationHandler(app);

    PlasmaTP::DockWatcher watcher;
    PlasmaTP::TouchpadOverlay overlay;
    // Sign-in for the dashboard Videos widget's subscriptions.
    PlasmaTP::YouTubeAccount youtube;

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
