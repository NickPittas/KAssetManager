#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <iostream>
#include "mainwindow.h"
#include "db.h"
#include "log_manager.h"
#include "progress_manager.h"
#include "thumbnail_generator.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

// FFmpeg log suppression (optional - only if headers are available)
#ifdef __has_include
#if __has_include(<libavutil/log.h>)
extern "C" {
#include <libavutil/log.h>
}
#define HAVE_FFMPEG_LOG
#endif
#endif

static QFile *logFile = nullptr;

static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString typeStr;

    switch (type) {
        case QtDebugMsg:    typeStr = "DEBUG"; break;
        case QtInfoMsg:     typeStr = "INFO "; break;
        case QtWarningMsg:  typeStr = "WARN "; break;
        case QtCriticalMsg: typeStr = "CRIT "; break;
        case QtFatalMsg:    typeStr = "FATAL"; break;
    }

    QString logLine = QString("[%1] [%2] %3\n").arg(timestamp).arg(typeStr).arg(msg);

    // Write to file
    if (logFile && logFile->isOpen()) {
        QTextStream stream(logFile);
        stream << logLine;
        stream.flush();
    }

    // Only write warnings and errors to console
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        fprintf(stderr, "%s", logLine.toLocal8Bit().constData());
        fflush(stderr);
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // Allocate console on Windows
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        std::cout.clear();
        std::cerr.clear();
    }
#endif

    // Suppress FFmpeg error messages to prevent console spam
#ifdef HAVE_FFMPEG_LOG
    av_log_set_level(AV_LOG_QUIET);
#endif

    QApplication app(argc, argv);

    // Identify app for QSettings
    QCoreApplication::setOrganizationName("KAsset");
    QCoreApplication::setOrganizationDomain("kasset.local");
    QCoreApplication::setApplicationName("KAsset Manager Qt");

    // Setup logging to file
    QString appDir = QCoreApplication::applicationDirPath();
    QString logPath = appDir + "/debug.log";
    logFile = new QFile(logPath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qInstallMessageHandler(messageHandler);
    }

    LogManager::instance().addLog("[MAIN] Message handler configured; app dir=" + appDir);
    // Initialize singletons
    auto& logManager = LogManager::instance();
    logManager.addLog("Application started");
    auto& progressManager = ProgressManager::instance();
    auto& thumbGen = ThumbnailGenerator::instance();
    LogManager::instance().addLog("[MAIN] Before DB init");

    // Init local SQLite DB under portable data folder
    const QString dataDir = appDir + "/data";
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/kasset.db";
    if (!DB::instance().init(dbPath)) {
        qCritical() << "Failed to initialize database at" << dbPath;
        return -1;
    }
    LogManager::instance().addLog("[MAIN] DB init ok");

    LogManager::instance().addLog("[MAIN] Creating MainWindow");
    // Create and show main window
    MainWindow mainWindow;
    LogManager::instance().addLog("[MAIN] MainWindow constructed");
    mainWindow.show();
    LogManager::instance().addLog("[MAIN] MainWindow shown");
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []{ LogManager::instance().addLog("[MAIN] aboutToQuit"); });
    QTimer::singleShot(0, []{ LogManager::instance().addLog("[MAIN] Event loop entered"); });

    int rc = app.exec();
    LogManager::instance().addLog(QString("[MAIN] Event loop exited with code %1").arg(rc));

    // Cleanup
    if (logFile) {
        logFile->close();
        delete logFile;
    }

    return rc;
}
