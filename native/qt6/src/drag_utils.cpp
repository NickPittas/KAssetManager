#include "drag_utils.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>
#include <QFile>
#include <QDesktopServices>
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
    QWindow *win = QGuiApplication::focusWindow();
    if (!win) win = QGuiApplication::activeWindow();
    QObject *dragParent = win ? static_cast<QObject*>(win) : static_cast<QObject*>(QGuiApplication::instance());

    auto *drag = new QDrag(dragParent);
    auto *mime = new QMimeData(drag);  // Set drag as parent for ownership
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
    QTemporaryDir tmp;
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
#else
    QFileInfo fi(path);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}


