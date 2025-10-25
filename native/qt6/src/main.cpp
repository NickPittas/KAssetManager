#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QDebug>
#include <QLoggingCategory>
#include <QUrl>
#include <QStringList>
#include <QtQuickControls2/QQuickStyle>
#include "drag_utils.h"
#include "db.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "importer.h"
#include "log_manager.h"
#include "progress_manager.h"
#include "thumbnail_generator.h"
#include "tags_model.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

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
    QGuiApplication app(argc, argv);

    // Identify app for QSettings/Settings
    QCoreApplication::setOrganizationName("KAsset");
    QCoreApplication::setOrganizationDomain("kasset.local");
    QCoreApplication::setApplicationName("KAsset Manager Qt");

    write_startup_log("[main] QGuiApplication created");

    // Initialize singletons early so they are ready for QML
    auto& logManager = LogManager::instance();
    logManager.addLog("Application started");
    auto& progressManager = ProgressManager::instance();
    auto& thumbGen = ThumbnailGenerator::instance();

    write_startup_log("[main] Singletons initialized");

    // Use a customizable style for Controls
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath());

    // Register QML-facing types and singletons
    qmlRegisterType<AssetsModel>("KAssetManager", 1, 0, "AssetsModel");
    qmlRegisterType<VirtualFolderTreeModel>("KAssetManager", 1, 0, "VirtualFolderTreeModel");
    qmlRegisterType<Importer>("KAssetManager", 1, 0, "Importer");
    qmlRegisterType<TagsModel>("KAssetManager", 1, 0, "TagsModel");
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "LogManager", &logManager);
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "ProgressManager", &progressManager);
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "ThumbnailGenerator", &thumbGen);

    // Init local SQLite DB under portable data folder
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dataDir = appDir + "/data";
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/kasset.db";
    write_startup_log(QString("[main] Initializing DB at %1").arg(dbPath));
    if (!DB::instance().init(dbPath)) {
        write_startup_log("[main] DB init FAILED");
        qCritical() << "Failed to initialize database at" << dbPath;
    } else {
        write_startup_log("[main] DB init OK");
    }


    QStringList arguments = QCoreApplication::arguments();
    bool diagnosticsEnabled = !qEnvironmentVariableIsEmpty("KASSET_DIAGNOSTICS") || arguments.contains(QStringLiteral("--diag"));
    auto log = [diagnosticsEnabled](const QString &msg){
        if (diagnosticsEnabled) {
            qInfo().noquote() << msg;
        }
    };
    log(QStringLiteral("[main] start"));

    // Capture QML warnings to the log and startup.log
    QObject::connect(&engine, &QQmlEngine::warnings, [&log](const QList<QQmlError> &warnings){
        for (const QQmlError &e : warnings) {
            const QString msg = QString("[QML Warning] %1 (%2:%3:%4)").arg(e.description()).arg(e.url().toString()).arg(e.line()).arg(e.column());
            write_startup_log(msg);
            log(msg);
        }
    });

    if (diagnosticsEnabled) {
        // Keep Qt subsystem debug/info logs disabled to avoid log storms that can freeze the UI.
        // Warnings and errors still surface via QQmlEngine::warnings and stderr.
        QLoggingCategory::setFilterRules(QStringLiteral(
            "qt.qml.debug=false\n"
            "qt.qml.info=false\n"
            "qt.quick.debug=false\n"
            "qt.quick.info=false\n"
            "qt.scenegraph.debug=false\n"
            "qt.scenegraph.info=false\n"
            "qt.multimedia.debug=false\n"
            "qt.multimedia.info=false\n"
        ));
    }

    // Load from embedded qrc to ensure the app runs outside the build tree
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&log](QObject *obj, const QUrl &objUrl) {
            Q_UNUSED(objUrl);
            write_startup_log(QString("[objectCreated] obj=%1").arg(obj ? "OK" : "NULL"));
            log(QString("[objectCreated] obj=%1").arg(obj ? "OK" : "NULL"));
            if (!obj)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&log]{ write_startup_log("[app] aboutToQuit"); log("[app] aboutToQuit"); });

    write_startup_log("[main] Loading QML from QRC");
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/KAssetManager/qml/Main.qml")));
    write_startup_log("[main] QML loaded");
    log("[main] load(QRC) returned");

    int rc = app.exec();
    write_startup_log(QString("[main] app.exec() returned rc=%1").arg(rc));
    log(QString("[main] app.exec() returned rc=%1").arg(rc));
    return rc;
}
