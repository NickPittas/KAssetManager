#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include "mainwindow.h"
#include "db.h"
#include "log_manager.h"
#include "progress_manager.h"
#include "thumbnail_generator.h"

static void write_startup_log(const QString &msg) {
    QString path = QCoreApplication::applicationDirPath() + "/startup.log";
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " " << msg << "\n";
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Identify app for QSettings
    QCoreApplication::setOrganizationName("KAsset");
    QCoreApplication::setOrganizationDomain("kasset.local");
    QCoreApplication::setApplicationName("KAsset Manager Qt");

    write_startup_log("[main] QApplication created");

    // Initialize singletons
    auto& logManager = LogManager::instance();
    logManager.addLog("Application started");
    auto& progressManager = ProgressManager::instance();
    auto& thumbGen = ThumbnailGenerator::instance();

    write_startup_log("[main] Singletons initialized");

    // Init local SQLite DB under portable data folder
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dataDir = appDir + "/data";
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/kasset.db";
    write_startup_log(QString("[main] Initializing DB at %1").arg(dbPath));
    if (!DB::instance().init(dbPath)) {
        write_startup_log("[main] DB init FAILED");
        qCritical() << "Failed to initialize database at" << dbPath;
        return -1;
    } else {
        write_startup_log("[main] DB init OK");
    }

    // Create and show main window
    write_startup_log("[main] Creating MainWindow");
    MainWindow mainWindow;
    mainWindow.show();
    write_startup_log("[main] MainWindow shown");

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []{
        write_startup_log("[app] aboutToQuit");
    });

    int rc = app.exec();
    write_startup_log(QString("[main] app.exec() returned rc=%1").arg(rc));
    return rc;
}
