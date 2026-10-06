#include <QtWidgets>
#include <QGuiApplication>
#include <QSessionManager>
#include <QLocalServer>
#include <QLocalSocket>
#include "mainwindow.h"
#include "i18n.h"
#include "text_size.h"
#include <functional>
#include <memory>
#include "profile.h"
#include "single_instance.h"
#include "app_version.h"

int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QApplication::setOrganizationName("Yusi");
    QApplication::setApplicationName("YusiManager");
    QApplication::setApplicationDisplayName("KeyBunker.org");
    QApplication::setApplicationVersion(YusiManagerVersion);
    QApplication::setDesktopFileName("yusimanager");
    QApplication::setWindowIcon(QIcon(":/yusimanager.png"));
    QApplication::setQuitOnLastWindowClosed(false);
    TextSize::load();
    const QString savedLanguage = QSettings().value("language", "en").toString();
    if (!I18n::setLanguage(savedLanguage, false)) I18n::setLanguage("en", false);

#if QT_CONFIG(sessionmanager)
    QObject::connect(&app, &QGuiApplication::commitDataRequest,
                     [&](QSessionManager &sm){ sm.setRestartHint(QSessionManager::RestartNever); });
    QObject::connect(&app, &QGuiApplication::saveStateRequest,
                     [&](QSessionManager &sm){ sm.setRestartHint(QSessionManager::RestartNever); });
#endif

    QString serverName = instanceServerName();
    if (notifyRunningInstance(serverName, "ACTIVATE")) return 0;

    QLocalServer server;
    QLocalServer::removeServer(serverName);
    if (!server.listen(serverName)) {
        QLocalServer::removeServer(serverName);
        server.listen(serverName);
    }

    bool startHidden = false;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]).trimmed();
        if (a == "--minimized" || a == "--hidden") { startHidden = true; break; }
    }

    QString dir;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            const QString a = QString::fromLocal8Bit(argv[i]).trimmed();
            if (a.startsWith("--")) continue;
            dir = a;
            break;
        }
    }
    if (dir.isEmpty()) {
        dir = defaultProfilesDirectory();
    }
    dir = QFileInfo(expandPath(dir)).absoluteFilePath();
    if (!QDir().mkpath(dir)) {
        QMessageBox::critical(nullptr, I18n::text("Profiles folder"),
                              QString(I18n::text("Could not create or open the profiles folder:\n%1")).arg(dir));
        return 1;
    }

    std::unique_ptr<MainWindow> window;
    std::function<void(const QString &, bool)> createWindow;
    createWindow = [&](const QString &directory, bool reopenSettings) {
        const QByteArray geometry = window ? window->saveGeometry() : QByteArray();
        if (window) {
            window->hide();
            window.release()->deleteLater();
        }
        window = std::make_unique<MainWindow>(directory);
        window->setWindowTitle("YusiManager " + QApplication::applicationVersion());
        window->setWindowIcon(qApp->windowIcon());
        if (!geometry.isEmpty()) window->restoreGeometry(geometry);
        QObject::connect(window.get(), &MainWindow::languageChangeRequested, &app, [&](bool reopen) {
            const QString directory = window->sitesDirectory();
            createWindow(directory, reopen);
        }, Qt::QueuedConnection);
        if (startHidden && QSystemTrayIcon::isSystemTrayAvailable()) {
            window->setWindowState(Qt::WindowMinimized);
        } else {
            window->show();
        }
        startHidden = false;
        if (reopenSettings) QTimer::singleShot(0, window.get(), &MainWindow::openGeneralSettings);
    };
    createWindow(dir, false);

    QObject::connect(&server, &QLocalServer::newConnection, &app, [&]{
        while (QLocalSocket *client = server.nextPendingConnection()) {
            client->waitForReadyRead(200);
            QByteArray msg = client->readAll();
            client->disconnectFromServer();
            client->deleteLater();
            if (msg.contains("ACTIVATE")) window->bringToFront();
        }
    });

    return app.exec();
}
