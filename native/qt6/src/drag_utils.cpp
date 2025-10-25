#include "drag_utils.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>
#include <QGuiApplication>
#include <QFileInfo>
#include <QDebug>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "virtual_drag.h"
#endif
#include <QQmlEngine>
#include <QJSEngine>


static void logLine(const QString &msg) {
    QFile f("startup.log");
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << msg << '\n';
    }
}

DragUtils* DragUtils::create(QQmlEngine*, QJSEngine*) {
    static DragUtils* instance = new DragUtils();
    return instance;
}


bool DragUtils::startFileDrag(const QStringList &paths) {
    if (paths.isEmpty()) return false;

    logLine(QString("[drag] startFileDrag count=%1 first='%2'")
            .arg(paths.size()).arg(paths.first()));

#ifdef _WIN32
    // Use native OLE CF_HDROP drag for maximum compatibility
    QVector<QString> v; v.reserve(paths.size());
    for (const auto &p : paths) v.push_back(p);
    bool ok = VirtualDrag::startRealPathsDrag(v);
    logLine(QString("[drag] startRealPathsDrag returned %1").arg(ok));
    return ok;
#else
    // Fallback to Qt cross-platform drag (non-Windows)
    auto *mime = new QMimeData();
    QList<QUrl> urls; urls.reserve(paths.size());
    for (const auto &p : paths) urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);
    QWindow *win = QGuiApplication::focusWindow();
    if (!win) win = QGuiApplication::activeWindow();
    QObject *dragParent = win ? static_cast<QObject*>(win) : static_cast<QObject*>(QGuiApplication::instance());
    auto *drag = new QDrag(dragParent);
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
    QString p = QDir::toNativeSeparators(path);
    QString param = QStringLiteral("/select,\"%1\"").arg(p);
    std::wstring wparam = param.toStdWString();
    HINSTANCE res = ShellExecuteW(nullptr, L"open", L"explorer.exe", wparam.c_str(), nullptr, SW_SHOWNORMAL);
    return (UINT_PTR)res > 32;
#else
    QFileInfo fi(path);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
}


