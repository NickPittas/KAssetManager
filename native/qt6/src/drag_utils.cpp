#include "drag_utils.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>
#include <QFile>
#include <QDesktopServices>
#include <QCoreApplication>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <ShlObj.h>
#include "virtual_drag.h"
#endif

DragUtils& DragUtils::instance() {
    static DragUtils instance;
    return instance;
}

bool DragUtils::startFileDrag(const QStringList &paths) {
    if (paths.isEmpty()) return false;

#ifdef _WIN32
    // Use native OLE CF_HDROP drag for maximum compatibility
    QVector<QString> v; v.reserve(paths.size());
    for (const auto &p : paths) v.push_back(p);
    return VirtualDrag::startRealPathsDrag(v);
#else
    // Fallback to Qt cross-platform drag (non-Windows)
    auto *drag = new QDrag(QApplication::instance());
    auto *mime = new QMimeData();
    QList<QUrl> urls; urls.reserve(paths.size());
    for (const auto &p : paths) urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);
    drag->setMimeData(mime);
    Qt::DropAction act = drag->exec(Qt::CopyAction);
    return act != Qt::IgnoreAction;
#endif
}


bool DragUtils::startVirtualDragSample() {
#ifdef _WIN32
    const QString name = "Virtual-From-App.txt";
    const QByteArray data = QByteArray("Hello from KAsset Manager (virtual file)\r\n");
    return VirtualDrag::startVirtualDrag(QVector<VirtualDrag::VirtualFile>{ {name, data} });
#else
    return false;
#endif
}

bool DragUtils::startVirtualDragSampleMulti() {
#ifdef _WIN32
    QVector<VirtualDrag::VirtualFile> files;
    files.push_back({ QStringLiteral("First.txt"),  QByteArray("First virtual file\r\n") });
    files.push_back({ QStringLiteral("Second.txt"), QByteArray("Second virtual file\r\n") });
    return VirtualDrag::startVirtualDrag(files);
#else
    return false;
#endif
}

bool DragUtils::startVirtualDragSampleFallbackCFHDrop() {
    const QString tempRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/data/tmp");
    QDir().mkpath(tempRoot);
    QTemporaryDir tmp(tempRoot + QStringLiteral("/drag-XXXXXX"));
    if (!tmp.isValid()) return false;
    const QString path = tmp.filePath("Virtual-From-App.txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    f.write("Hello from KAsset Manager (temp file fallback)\r\n");
    f.close();
    // QTemporaryDir keeps the directory alive until it goes out of scope; drag->exec blocks until drop finishes
    return startFileDrag(QStringList{ path });
}

bool DragUtils::showInExplorer(const QString &path) {
#ifdef _WIN32
    const QString native = QDir::toNativeSeparators(path);
    const std::wstring w = native.toStdWString();
    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(w.c_str());
    if (!pidl) return false;
    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    ILFree(pidl);
    return SUCCEEDED(hr);
#elif defined(Q_OS_LINUX)
    const QFileInfo fi(path);
    const QString targetPath = fi.exists() ? fi.absoluteFilePath() : path;
    const QUrl targetUrl = QUrl::fromLocalFile(targetPath);

    QDBusInterface fileManager(
        QStringLiteral("org.freedesktop.FileManager1"),
        QStringLiteral("/org/freedesktop/FileManager1"),
        QStringLiteral("org.freedesktop.FileManager1"),
        QDBusConnection::sessionBus());

    if (fileManager.isValid()) {
        const QStringList uris{targetUrl.toString()};
        const QDBusReply<void> reply = fileManager.call(
            QStringLiteral("ShowItems"),
            uris,
            QString());
        if (reply.isValid()) {
            return true;
        }
        qWarning() << "[DragUtils] FileManager1 ShowItems failed for" << path << ':' << reply.error().message();
    }

    const QString fallbackPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fallbackPath));
#else
    QFileInfo fi(path);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}
