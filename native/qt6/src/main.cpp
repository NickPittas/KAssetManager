#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QFileInfo>
#include <iostream>
#include "mainwindow.h"
#include "db.h"
#include "log_manager.h"
#include "progress_manager.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <DbgHelp.h>
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


int main(int argc, char *argv[])
{

    // Suppress FFmpeg error messages to prevent console spam
#ifdef HAVE_FFMPEG_LOG
    av_log_set_level(AV_LOG_QUIET);
#endif

    QApplication app(argc, argv);

    // Identify app for QSettings
    QCoreApplication::setOrganizationName("KAsset");
    QCoreApplication::setOrganizationDomain("kasset.local");
    QCoreApplication::setApplicationName("KAsset Manager Qt");

    // Install centralized message handler that logs via LogManager to app.log
    QString appDir = QCoreApplication::applicationDirPath();
    qInstallMessageHandler(customMessageHandler);

#ifdef Q_OS_WIN
    // Install a top-level SEH filter to capture crashes and write a minidump
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        // Use persistent user data location for crash dumps
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        QString dumpPath = dataDir + "/crash.dmp";
        HANDLE hFile = CreateFileW((LPCWSTR)dumpPath.utf16(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei;
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = ep;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpWithIndirectlyReferencedMemory, &mei, NULL, NULL);
            CloseHandle(hFile);
        }
        QFile f(dataDir + "/crash.log");
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            const QString tsNow = QDateTime::currentDateTime().toString(Qt::ISODate);
#ifdef QT_NO_DEBUG
            // In release builds, avoid logging raw addresses to limit information disclosure
            ts << tsNow << " Crash: code=0x" << QString::number(ep->ExceptionRecord->ExceptionCode, 16) << "\n";
#else
            // In debug builds, include full details for diagnosis
            ts << tsNow << " Crash: code=0x" << QString::number(ep->ExceptionRecord->ExceptionCode, 16)
               << " addr=0x" << QString::number(reinterpret_cast<qulonglong>(ep->ExceptionRecord->ExceptionAddress), 16) << "\n";
#endif
        }
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif

    LogManager::instance().addLog("[MAIN] Message handler configured; app dir=" + appDir);
    // Initialize singletons
    auto& logManager = LogManager::instance();
    logManager.addLog("Application started");
    auto& progressManager = ProgressManager::instance();
    LogManager::instance().addLog("[MAIN] Before DB init");

    // Use persistent user data location for database (survives app updates)
    // On Windows: C:/Users/[Username]/AppData/Roaming/KAsset/KAsset Manager Qt/
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/kasset.db";

    // Migration: Move database from old location (appDir/data) to new location (AppData)
    const QString oldDataDir = appDir + "/data";
    const QString oldDbPath = oldDataDir + "/kasset.db";
    if (!QFile::exists(dbPath) && QFile::exists(oldDbPath)) {
        LogManager::instance().addLog("[MAIN] Migrating database from old location to persistent location");
        LogManager::instance().addLog("[MAIN] Old: " + oldDbPath);
        LogManager::instance().addLog("[MAIN] New: " + dbPath);

        // Copy database file
        if (QFile::copy(oldDbPath, dbPath)) {
            LogManager::instance().addLog("[MAIN] Database migrated successfully");

            // Migrate versions directory if it exists
            const QString oldVersionsDir = oldDataDir + "/versions";
            const QString newVersionsDir = dataDir + "/versions";
            if (QDir(oldVersionsDir).exists()) {
                LogManager::instance().addLog("[MAIN] Migrating versions directory");
                QDir().mkpath(newVersionsDir);

                // Copy all version subdirectories
                QDir oldDir(oldVersionsDir);
                QStringList assetDirs = oldDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                for (const QString& assetDir : assetDirs) {
                    QString srcPath = oldVersionsDir + "/" + assetDir;
                    QString dstPath = newVersionsDir + "/" + assetDir;
                    QDir().mkpath(dstPath);

                    QDir srcDir(srcPath);
                    QStringList files = srcDir.entryList(QDir::Files);
                    for (const QString& file : files) {
                        QFile::copy(srcPath + "/" + file, dstPath + "/" + file);
                    }
                }
                LogManager::instance().addLog("[MAIN] Versions migrated successfully");
            }

            // Optionally remove old database (keep for safety - user can delete manually)
            // QFile::remove(oldDbPath);
            LogManager::instance().addLog("[MAIN] Old database preserved at: " + oldDbPath);
        } else {
            LogManager::instance().addLog("[MAIN] WARNING: Failed to migrate database, will use old location");
            // Fall back to old location if migration fails
            const QString fallbackDbPath = oldDbPath;
            if (!DB::instance().init(fallbackDbPath)) {
                qCritical() << "Failed to initialize database at" << fallbackDbPath;
                return -1;
            }
            LogManager::instance().addLog("[MAIN] DB init ok (using old location)");
            goto skip_new_init;
        }
    }

    if (!DB::instance().init(dbPath)) {
        qCritical() << "Failed to initialize database at" << dbPath;
        return -1;
    }
    LogManager::instance().addLog("[MAIN] DB init ok at: " + dbPath);

skip_new_init:

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

    // Cleanup (LogManager flushes automatically on destruction)
    return rc;
}
