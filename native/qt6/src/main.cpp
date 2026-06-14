#include <QApplication>
#include <QGuiApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QFileInfo>
#include <QByteArray>
#include <clocale>
#include <iostream>
#include "mainwindow.h"
#include "db.h"
#include "log_manager.h"
#include "progress_manager.h"
#include "runtime_paths.h"
#include "theme_manager.h"
#include "platform_session.h"
#ifdef HAVE_TLRENDER
#include "media/player_lab_player.h"
#endif

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
    setlocale(LC_NUMERIC, "C");

    // High DPI Configuration - MUST be set before QApplication is created
    // Qt 6 enables High DPI scaling by default, but we need to configure it properly
    // to prevent blurry rendering and ensure consistent UI across different DPI displays.
    //
    // PassThrough: Use exact device pixel ratio without rounding (prevents blurry text)
    // This is the recommended policy for Qt 6 applications.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // Enable OpenGL context sharing for multiple QOpenGLWidgets.
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#if defined(Q_OS_LINUX)
    if (PlatformSession::isWayland() && PlatformSession::shouldForceRasterWidgetsOnWayland()) {
        QApplication::setAttribute(Qt::AA_ForceRasterWidgets);
    }
    if (PlatformSession::isWayland() && qEnvironmentVariableIsEmpty("QT_WIDGETS_RHI")) {
        // Qt's widget RHI path is the codepath emitting
        // "Failed to create QRhi for QBackingStoreRhiSupport" on Fedora Wayland
        // when a tlRender QOpenGLWidget preview is created on demand.
        qputenv("QT_WIDGETS_RHI", QByteArrayLiteral("0"));
    }
#endif

    // Use desktop OpenGL for widget-based playback surfaces.
    // On Wayland we avoid forcing tlRender's global 4.1 default format, but still
    // prefer the desktop GL backend so the tlRender QOpenGLWidget can create a context.
#if !defined(Q_OS_LINUX)
    QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
#elif defined(Q_OS_LINUX)
    if (PlatformSession::isWayland() && PlatformSession::shouldUseDesktopOpenGLOnWayland()) {
        QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
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
    QCoreApplication::setApplicationVersion(QStringLiteral(KAM_APP_VERSION));

    // Load and apply theme (palette + minimal stylesheet) before any widgets are created
    ThemeManager::instance().loadTheme();

    // Install centralized message handler that logs via LogManager.
    QString appDir = QCoreApplication::applicationDirPath();
    qInstallMessageHandler(customMessageHandler);

#ifdef Q_OS_WIN
    // Install a top-level SEH filter to capture crashes and write a minidump
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        const QString dataDir = RuntimePaths::writableDataRoot();
        const QString dumpPath = RuntimePaths::dataPath("crash.dmp");
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

    const QString dataDir = RuntimePaths::writableDataRoot();
    RuntimePaths::migrateLegacyDataToWritableRoot();
    const QString dbPath = RuntimePaths::dataPath("kasset.db");
    LogManager::instance().addLog("[MAIN] Writable data root: " + dataDir);
    LogManager::instance().addLog(QString("[MAIN] Data mode: %1")
        .arg(RuntimePaths::usingPortableDataRoot() ? "portable" : "per-user"));

    if (!DB::instance().init(dbPath)) {
        qCritical() << "Failed to initialize database at" << dbPath;
        return -1;
    }
    LogManager::instance().addLog("[MAIN] DB init ok at: " + dbPath);

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
