#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlError>
#include <QDebug>
#include <QLoggingCategory>
#include <QUrl>
#include <QStringList>
#include "drag_utils.h"
#include "db.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "importer.h"
#include "log_manager.h"
#include "progress_manager.h"
#include "thumbnail_generator.h"
#include <QDir>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // Initialize singletons early so they are ready for QML
    auto& logManager = LogManager::instance();
    logManager.addLog("Application started");
    auto& progressManager = ProgressManager::instance();
    auto& thumbGen = ThumbnailGenerator::instance();

    QQmlApplicationEngine engine;

    // Register QML-facing types and singletons
    qmlRegisterType<AssetsModel>("KAssetManager", 1, 0, "AssetsModel");
    qmlRegisterType<VirtualFolderTreeModel>("KAssetManager", 1, 0, "VirtualFolderTreeModel");
    qmlRegisterType<Importer>("KAssetManager", 1, 0, "Importer");
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "LogManager", &logManager);
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "ProgressManager", &progressManager);
    qmlRegisterSingletonInstance("KAssetManager", 1, 0, "ThumbnailGenerator", &thumbGen);

    // Init local SQLite DB under portable data folder
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dataDir = appDir + "/data";
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/kasset.db";
    if (!DB::instance().init(dbPath)) {
        qCritical() << "Failed to initialize database at" << dbPath;
    }


    QStringList arguments = QCoreApplication::arguments();
    bool diagnosticsEnabled = !qEnvironmentVariableIsEmpty("KASSET_DIAGNOSTICS") || arguments.contains(QStringLiteral("--diag"));
    auto log = [diagnosticsEnabled](const QString &msg){
        if (diagnosticsEnabled) {
            qInfo().noquote() << msg;
        }
    };
    log(QStringLiteral("[main] start"));

    // Capture QML warnings to the log
    QObject::connect(&engine, &QQmlEngine::warnings, [&log](const QList<QQmlError> &warnings){
        for (const QQmlError &e : warnings) {
            log(QString("[QML Warning] %1 (%2:%3:%4)").arg(e.description()).arg(e.url().toString()).arg(e.line()).arg(e.column()));
        }
    });

    if (diagnosticsEnabled) {
        QLoggingCategory::setFilterRules(QStringLiteral("qt.qml.*=true\nqt.quick.*=true\nqt.scenegraph.*=true\nqt.multimedia.*=true"));
    }

    // Load from embedded qrc to ensure the app runs outside the build tree
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
        [&log](QObject *obj, const QUrl &objUrl) {
            Q_UNUSED(objUrl);
            log(QString("[objectCreated] obj=%1").arg(obj ? "OK" : "NULL"));
            if (!obj)
                QCoreApplication::exit(-1);
        }, Qt::QueuedConnection);

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&log]{ log("[app] aboutToQuit"); });

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/KAssetManager/qml/Main.qml")));
    log("[main] load(QRC) returned");

    int rc = app.exec();
    log(QString("[main] app.exec() returned rc=%1").arg(rc));
    return rc;
}
