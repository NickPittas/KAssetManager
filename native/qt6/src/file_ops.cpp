#include "file_ops.h"

#include <QtConcurrent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QMessageBox>
#include <QCoreApplication>
#include <QMetaObject>
#include <QCheckBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QDebug>
#include <QStack>

#include <functional>

#include "file_utils.h"
#include <QApplication>
#include <QWidget>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <combaseapi.h>
#include <comdef.h>
#endif

static QString typeToString(FileOpsQueue::Type t) {
    switch (t) {
        case FileOpsQueue::Type::Copy: return "Copy";
        case FileOpsQueue::Type::Move: return "Move";
        case FileOpsQueue::Type::Delete: return "Delete";
    }
    return "";
}

#ifndef _WIN32
static bool samePath(const QString& a, const QString& b)
{
    return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}

static QString uniqueNameInDirNonWindows(const QString& dir, const QString& baseName)
{
    QString name = baseName;
    QString path = QDir(dir).filePath(name);
    int i = 2;
    while (FileUtils::pathExists(path)) {
        const QFileInfo fi(baseName);
        const QString stem = fi.completeBaseName();
        const QString ext = fi.suffix();
        if (ext.isEmpty()) name = QString("%1 (%2)").arg(stem).arg(i++);
        else name = QString("%1 (%2).%3").arg(stem).arg(i++).arg(ext);
        path = QDir(dir).filePath(name);
    }
    return path;
}

struct FileOpProgressState
{
    qint64 completedFiles = 0;
    qint64 totalFiles = 0;
    qint64 completedWork = 0;
    qint64 totalWork = 0;
    qint64 currentWork = 0;
    QString currentFile;
};

struct FileOpEstimate
{
    qint64 fileCount = 0;
    qint64 workUnits = 0;
};

static FileOpEstimate estimatePath(const QString& path)
{
    FileOpEstimate estimate;
    QFileInfo rootInfo(path);
    if (!rootInfo.exists()) {
        return estimate;
    }

    if (rootInfo.isFile()) {
        estimate.fileCount = 1;
        estimate.workUnits = qMax<qint64>(1, rootInfo.size());
        return estimate;
    }

    QStack<QString> pending;
    pending.push(rootInfo.absoluteFilePath());
    while (!pending.isEmpty()) {
        const QString currentPath = pending.pop();
        const QFileInfo currentInfo(currentPath);
        if (currentInfo.isDir()) {
            estimate.workUnits += 1;
            const QFileInfoList entries = QDir(currentPath).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
            for (const QFileInfo& entry : entries) {
                pending.push(entry.absoluteFilePath());
            }
        } else {
            estimate.fileCount += 1;
            estimate.workUnits += qMax<qint64>(1, currentInfo.size());
        }
    }

    if (estimate.fileCount == 0) {
        estimate.fileCount = 1;
    }
    return estimate;
}

static qint64 estimatePathWorkUnits(const QString& path)
{
    return estimatePath(path).workUnits;
}

static qint64 estimateBatchWorkUnits(const QStringList& paths)
{
    qint64 total = 0;
    for (const QString& path : paths) {
        total += estimatePathWorkUnits(path);
    }
    return qMax<qint64>(1, total);
}

static qint64 estimateBatchFileCount(const QStringList& paths)
{
    qint64 total = 0;
    for (const QString& path : paths) {
        total += estimatePath(path).fileCount;
    }
    return qMax<qint64>(1, total);
}

static bool removeRecursivelyNonWindowsWithProgress(const QString& path,
                                                    std::atomic_bool& cancel,
                                                    FileOpProgressState* progress,
                                                    const std::function<void(const FileOpProgressState&)>& reportProgress,
                                                    QString* errorOut)
{
    if (!FileUtils::pathExists(path)) return true;

    const QFileInfo fi(path);
    if (cancel.load()) return false;

    if (fi.isDir()) {
        const QFileInfoList entries = QDir(path).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo& entry : entries) {
            if (!removeRecursivelyNonWindowsWithProgress(entry.absoluteFilePath(), cancel, progress, reportProgress, errorOut)) {
                return false;
            }
        }
        if (!QDir().rmdir(path)) {
            if (errorOut) *errorOut = QObject::tr("Failed to permanently delete: %1").arg(path);
            return false;
        }
        if (progress) {
            progress->currentFile = path;
            progress->completedWork += 1;
            progress->currentWork = progress->completedWork;
            reportProgress(*progress);
        }
        return true;
    }

    const qint64 fileWork = qMax<qint64>(1, fi.size());
    if (!QFile::remove(path)) {
        if (errorOut) *errorOut = QObject::tr("Failed to permanently delete: %1").arg(path);
        return false;
    }
    if (progress) {
        progress->currentFile = path;
        progress->completedFiles += 1;
        progress->completedWork += fileWork;
        progress->currentWork = progress->completedWork;
        reportProgress(*progress);
    }
    return true;
}

static bool copyFileWithProgressNonWindows(const QString& src,
                                           const QString& dst,
                                           std::atomic_bool& cancel,
                                           FileOpProgressState* progress,
                                           const std::function<void(const FileOpProgressState&)>& reportProgress,
                                           QString* errorOut)
{
    QFile in(src);
    QFile out(dst);
    if (!in.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QObject::tr("Failed to open %1").arg(src);
        return false;
    }
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!out.open(QIODevice::WriteOnly)) {
        if (errorOut) *errorOut = QObject::tr("Failed to write %1").arg(dst);
        return false;
    }
    const qint64 totalBytes = qMax<qint64>(1, in.size());
    qint64 copiedBytes = 0;
    const qint64 baseWork = progress ? progress->completedWork : 0;
    QByteArray buf;
    buf.resize(4 * 1024 * 1024);
    while (!in.atEnd()) {
        if (cancel.load()) {
            out.close();
            out.remove();
            return false;
        }
        const qint64 r = in.read(buf.data(), buf.size());
        if (r <= 0) break;
        const qint64 w = out.write(buf.constData(), r);
        if (w != r) {
            if (errorOut) *errorOut = QObject::tr("Write error %1").arg(dst);
            out.close();
            return false;
        }
        copiedBytes += w;
        if (progress) {
            progress->currentFile = src;
            progress->currentWork = baseWork + copiedBytes;
            reportProgress(*progress);
        }
    }
    out.flush();
    out.close();
    in.close();
    if (progress) {
        progress->completedFiles += 1;
        progress->completedWork += totalBytes;
        progress->currentWork = progress->completedWork;
        progress->currentFile = src;
        reportProgress(*progress);
    }
    return true;
}

static bool copyRecursivelyNonWindows(const QString& src,
                                      const QString& dst,
                                      std::atomic_bool& cancel,
                                      FileOpProgressState* progress,
                                      const std::function<void(const FileOpProgressState&)>& reportProgress,
                                      QString* errorOut)
{
    const QFileInfo sourceInfo(src);
    if (sourceInfo.isDir()) {
        QDir dstDir(dst);
        if (!dstDir.exists() && !QDir().mkpath(dst)) {
            if (errorOut) *errorOut = QObject::tr("Failed to create directory %1").arg(dst);
            return false;
        }
        if (progress) {
            progress->currentFile = src;
            progress->completedWork += 1;
            progress->currentWork = progress->completedWork;
            reportProgress(*progress);
        }
        const QFileInfoList entries = QDir(src).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo& entry : entries) {
            if (cancel.load()) return false;
            const QString target = dstDir.filePath(entry.fileName());
            if (!copyRecursivelyNonWindows(entry.absoluteFilePath(), target, cancel, progress, reportProgress, errorOut)) {
                return false;
            }
        }
        return true;
    }
    return copyFileWithProgressNonWindows(src, dst, cancel, progress, reportProgress, errorOut);
}

static bool copyEntryToDestination(const QString& src,
                                   const QString& destinationDir,
                                   std::atomic_bool& cancel,
                                   FileOpProgressState* progress,
                                   const std::function<void(const FileOpProgressState&)>& reportProgress,
                                   QString* errorOut)
{
    const QFileInfo sourceInfo(src);
    if (!sourceInfo.exists()) {
        if (errorOut) *errorOut = QObject::tr("Source does not exist: %1").arg(src);
        return false;
    }

    const QString targetPath = uniqueNameInDirNonWindows(destinationDir, sourceInfo.fileName());
    if (sourceInfo.isDir()) {
        return copyRecursivelyNonWindows(src, targetPath, cancel, progress, reportProgress, errorOut);
    }
    return copyFileWithProgressNonWindows(src, targetPath, cancel, progress, reportProgress, errorOut);
}

static bool moveEntryToDestination(const QString& src,
                                   const QString& destinationDir,
                                   std::atomic_bool& cancel,
                                   FileOpProgressState* progress,
                                   const std::function<void(const FileOpProgressState&)>& reportProgress,
                                   QString* errorOut)
{
    const QFileInfo sourceInfo(src);
    if (!sourceInfo.exists()) {
        if (errorOut) *errorOut = QObject::tr("Source does not exist: %1").arg(src);
        return false;
    }

    QString targetPath = QDir(destinationDir).filePath(sourceInfo.fileName());
    if (samePath(src, targetPath)) {
        return true;
    }
    if (FileUtils::pathExists(targetPath)) {
        targetPath = uniqueNameInDirNonWindows(destinationDir, sourceInfo.fileName());
    }

    if (QDir().rename(src, targetPath)) {
        if (progress) {
            progress->completedFiles += estimatePath(src).fileCount;
            progress->completedWork += estimatePathWorkUnits(src);
            progress->currentWork = progress->completedWork;
            progress->currentFile = src;
            reportProgress(*progress);
        }
        return true;
    }

    if (!copyEntryToDestination(src, destinationDir, cancel, progress, reportProgress, errorOut)) {
        return false;
    }

    if (!removeRecursivelyNonWindowsWithProgress(src, cancel, nullptr, [](const FileOpProgressState&) {}, errorOut)) {
        if (errorOut && errorOut->isEmpty()) {
            *errorOut = QObject::tr("Moved copy created, but failed to remove original: %1").arg(src);
        }
        return false;
    }
    return true;
}

static bool deleteEntry(const QString& path,
                        bool permanent,
                        std::atomic_bool& cancel,
                        FileOpProgressState* progress,
                        const std::function<void(const FileOpProgressState&)>& reportProgress,
                        QString* errorOut)
{
    if (!FileUtils::pathExists(path)) {
        return true;
    }
    if (cancel.load()) {
        return false;
    }

    if (!permanent) {
        QString pathInTrash;
        if (QFile::moveToTrash(path, &pathInTrash)) {
            if (progress) {
                progress->currentFile = path;
                progress->completedFiles += estimatePath(path).fileCount;
                progress->completedWork += estimatePathWorkUnits(path);
                progress->currentWork = progress->completedWork;
                reportProgress(*progress);
            }
            return true;
        }
        if (errorOut) {
            *errorOut = QObject::tr("Failed to move to trash: %1").arg(path);
        }
        return false;
    }

    return removeRecursivelyNonWindowsWithProgress(path, cancel, progress, reportProgress, errorOut);
}
#endif

FileOpsQueue& FileOpsQueue::instance()
{
    static FileOpsQueue inst;
    return inst;
}

FileOpsQueue::FileOpsQueue(QObject* parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<void>::finished, this, [this]{
        QMutexLocker lk(&m_mutex);
        m_running = false;
        emit queueChanged();
        lk.unlock();
        startNext();
    });
}

QList<FileOpsQueue::Item> FileOpsQueue::items() const
{
    QMutexLocker lk(&m_mutex);
    return m_queue;
}

bool FileOpsQueue::isBusy() const
{
    return m_running;
}

void FileOpsQueue::enqueueCopy(const QStringList& sources, const QString& destination)
{
    if (sources.isEmpty()) return;
    QMutexLocker lk(&m_mutex);
    Item it; it.id = m_nextId++; it.type = Type::Copy; it.sources = sources; it.destination = destination; it.status = "Queued"; it.totalFiles = sources.size();
    m_queue.push_back(it);
    emit queueChanged();
    if (!m_running) {
        lk.unlock();
        startNext();
    }
}

void FileOpsQueue::enqueueMove(const QStringList& sources, const QString& destination)
{
    if (sources.isEmpty()) return;
    QMutexLocker lk(&m_mutex);
    Item it; it.id = m_nextId++; it.type = Type::Move; it.sources = sources; it.destination = destination; it.status = "Queued"; it.totalFiles = sources.size();
    m_queue.push_back(it);
    emit queueChanged();
    if (!m_running) { lk.unlock(); startNext(); }
}

void FileOpsQueue::enqueueDelete(const QStringList& sources)
{
    if (sources.isEmpty()) return;
    QMutexLocker lk(&m_mutex);
    Item it; it.id = m_nextId++; it.type = Type::Delete; it.sources = sources; it.status = "Queued"; it.totalFiles = sources.size(); it.permanentDelete = false;
    m_queue.push_back(it);
    emit queueChanged();
    if (!m_running) { lk.unlock(); startNext(); }
}

void FileOpsQueue::enqueueDeletePermanent(const QStringList& sources)
{
    if (sources.isEmpty()) return;
    QMutexLocker lk(&m_mutex);
    Item it; it.id = m_nextId++; it.type = Type::Delete; it.sources = sources; it.status = "Queued"; it.totalFiles = sources.size(); it.permanentDelete = true;
    m_queue.push_back(it);
    emit queueChanged();
    if (!m_running) { lk.unlock(); startNext(); }
}

void FileOpsQueue::cancelCurrent()
{
    m_cancel.store(true);
}

void FileOpsQueue::cancelAll()
{
    m_cancel.store(true);
    QMutexLocker lk(&m_mutex);
    for (auto &it : m_queue) {
        if (it.status == "Queued") it.status = "Cancelled";
    }
    emit queueChanged();
}

void FileOpsQueue::startNext()
{
    QMutexLocker lk(&m_mutex);
    if (m_running) return;
    // find next queued
    int idx = -1;
    for (int i=0;i<m_queue.size();++i) { if (m_queue[i].status == "Queued") { idx = i; break; } }
    if (idx < 0) return; // nothing to do

    Item &item = m_queue[idx];
    item.status = "In Progress";
    item.completedFiles = 0;
    item.completedWork = 0;
    m_cancel.store(false);
    m_running = true;

    const int itemId = item.id;
    const auto type = item.type;
    const QStringList sources = item.sources;
    const QString dest = item.destination;
    const bool permanent = item.permanentDelete;

    emit currentItemChanged(item);
    emit queueChanged();

    // OS will handle preflight checks (space, permissions, conflicts)

    // Capture owner window handle on UI thread so OS dialogs have a parent
#ifdef _WIN32
    HWND ownerHwnd = nullptr;
    QWidget* w = QApplication::activeWindow();
    if (!w) {
        const auto tls = QApplication::topLevelWidgets();
        for (QWidget* tw : tls) {
            if (tw && tw->isVisible()) { w = tw; break; }
        }
    }
    if (w) ownerHwnd = reinterpret_cast<HWND>(w->winId());
#endif

    // Run in background using OS handlers
    m_future = QtConcurrent::run([this, itemId, type, sources, dest, permanent
#ifdef _WIN32
        , ownerHwnd
#endif
    ]() {
        bool success = false;
        bool aborted = false;
        QString opError;
        qInfo() << "[FileOps] Start" << typeToString(type)
                << "sources:" << sources
                << (type != Type::Delete ? QString("dest=%1").arg(dest) : QString());
        QString codeStr;

#ifdef _WIN32
        // Prefer modern IFileOperation to ensure proper OS UI dialogs
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool needUninit = SUCCEEDED(hrCo);
        // Initialize COM security (ignore RPC_E_TOO_LATE)
        HRESULT hrSec = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                             RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IDENTIFY,
                                             nullptr, EOAC_NONE, nullptr);
        Q_UNUSED(hrSec);

        IFileOperation* pfo = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfo));
        if (SUCCEEDED(hr) && pfo) {
            qInfo() << "[FileOps] Using IFileOperation";
            // Owner window for modal OS dialogs
            if (ownerHwnd) pfo->SetOwnerWindow(ownerHwnd);

            FILEOP_FLAGS flags = 0;
            flags |= FOF_NOCONFIRMMKDIR; // don't prompt to create directories
            if (type == Type::Delete && !permanent) flags |= FOF_ALLOWUNDO; // Recycle Bin
            pfo->SetOperationFlags(flags);

            // Destination (for copy/move)
            IShellItem* psiDest = nullptr;
            if (type != Type::Delete) {
                const QString destNative = QDir::toNativeSeparators(dest);
                hr = SHCreateItemFromParsingName((LPCWSTR)destNative.utf16(), nullptr, IID_PPV_ARGS(&psiDest));
                if (FAILED(hr)) {
                    _com_error ce(hr);
                    opError = QString("Failed to resolve destination: %1 (%2)")
                              .arg(destNative)
                              .arg(QString::fromWCharArray(ce.ErrorMessage()));
                }
            }

            // Queue operations
            if (opError.isEmpty()) {
                for (const QString &s0 : sources) {
                    const QString s = QDir::toNativeSeparators(s0);
                    IShellItem* psiSrc = nullptr;
                    HRESULT hrSrc = SHCreateItemFromParsingName((LPCWSTR)s.utf16(), nullptr, IID_PPV_ARGS(&psiSrc));
                    if (FAILED(hrSrc) || !psiSrc) {
                        opError = QString("Failed to resolve source: %1").arg(s);
                        break;
                    }
                    if (type == Type::Copy) {
                        hr = pfo->CopyItem(psiSrc, psiDest, nullptr, nullptr);
                    } else if (type == Type::Move) {
                        hr = pfo->MoveItem(psiSrc, psiDest, nullptr, nullptr);
                    } else { // Delete
                        hr = pfo->DeleteItem(psiSrc, nullptr);
                    }
                    psiSrc->Release();
                    if (FAILED(hr)) {
                        _com_error ce(hr);
                        opError = QString::fromWCharArray(ce.ErrorMessage());
                        break;
                    }
                }
            }

            if (opError.isEmpty()) {
                hr = pfo->PerformOperations();
                if (FAILED(hr)) {
                    _com_error ce(hr);
                    opError = QString::fromWCharArray(ce.ErrorMessage());
                }
                BOOL anyAborted = FALSE;
                if (SUCCEEDED(pfo->GetAnyOperationsAborted(&anyAborted))) {
                    aborted = (anyAborted != FALSE);
                }
                success = SUCCEEDED(hr) && !aborted;
                codeStr = QString("0x%1").arg(QString::number((quint32)hr, 16).toUpper());
            }

            if (psiDest) psiDest->Release();
            pfo->Release();
        } else {
            qInfo() << "[FileOps] Using SHFileOperation fallback, CoCreateInstance hr="
                    << QString("0x%1").arg(QString::number((quint32)hr, 16).toUpper());
            // Fallback: SHFileOperation (legacy)
            auto makeDoubleNullList = [](const QStringList& paths) -> std::wstring {
                std::wstring result;
                for (const auto &p0 : paths) {
                    const QString p = QDir::toNativeSeparators(p0);
                    std::wstring w = p.toStdWString();
                    result.append(w);
                    result.push_back(L'\0');
                }
                result.push_back(L'\0');
                return result;
            };
            SHFILEOPSTRUCTW op{}; ZeroMemory(&op, sizeof(op)); op.hwnd = ownerHwnd;
            std::wstring fromList = makeDoubleNullList(sources); op.pFrom = fromList.c_str();
            std::wstring toList; if (type != Type::Delete) { toList = makeDoubleNullList(QStringList{dest}); op.pTo = toList.c_str(); }
            if (type == Type::Copy) op.wFunc = FO_COPY; else if (type == Type::Move) op.wFunc = FO_MOVE; else if (type == Type::Delete) op.wFunc = FO_DELETE;
            op.fFlags = 0; if (type == Type::Delete && !permanent) op.fFlags |= FOF_ALLOWUNDO; op.fFlags |= FOF_NOCONFIRMMKDIR;
            int r = SHFileOperationW(&op); aborted = (op.fAnyOperationsAborted != FALSE); success = (r == 0) && !aborted;
            codeStr = QString::number(r);
            if (!success && r != 0) {
                wchar_t* buf = nullptr;
                DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                           nullptr, (DWORD)r, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buf, 0, nullptr);
                QString sysMsg = len && buf ? QString::fromWCharArray(buf).trimmed() : QString();
                if (buf) LocalFree(buf);
                opError = sysMsg.isEmpty() ? QString("OS file operation failed (code %1)").arg(r)
                                           : QString("OS file operation failed (code %1): %2").arg(r).arg(sysMsg);
            }
        }

        if (needUninit) CoUninitialize();
        qInfo() << "[FileOps] Done" << typeToString(type)
                << "success=" << success << "aborted=" << aborted << "code=" << codeStr
                << (opError.isEmpty() ? QString() : QString("error=%1").arg(opError));
#else
        FileOpProgressState progress;
        progress.totalFiles = estimateBatchFileCount(sources);
        progress.totalWork = estimateBatchWorkUnits(sources);
        progress.currentWork = 0;
        auto markProgress = [this, itemId](const FileOpProgressState& progressState) {
            {
                QMutexLocker lk(&m_mutex);
                for (Item &queued : m_queue) {
                    if (queued.id == itemId) {
                        queued.completedFiles = progressState.completedFiles;
                        queued.totalFiles = progressState.totalFiles;
                        queued.completedWork = progressState.currentWork;
                        queued.totalWork = progressState.totalWork;
                        queued.currentFile = progressState.currentFile;
                        break;
                    }
                }
            }
            emit progressChanged(progressState.currentWork, progressState.totalWork, progressState.currentFile);
            emit queueChanged();
        };
        markProgress(progress);

        success = true;
        for (const QString& source : sources) {
            if (m_cancel.load()) {
                aborted = true;
                success = false;
                break;
            }

            QString localError;
            bool stepOk = false;
            if (type == Type::Copy) {
                stepOk = copyEntryToDestination(source, dest, m_cancel, &progress, markProgress, &localError);
            } else if (type == Type::Move) {
                stepOk = moveEntryToDestination(source, dest, m_cancel, &progress, markProgress, &localError);
            } else {
                stepOk = deleteEntry(source, permanent, m_cancel, &progress, markProgress, &localError);
            }

            if (!stepOk) {
                if (m_cancel.load()) {
                    aborted = true;
                }
                success = false;
                opError = localError;
                break;
            }

            progress.currentFile = source;
            progress.currentWork = progress.completedWork;
            markProgress(progress);
        }

        if (!success && opError.isEmpty() && aborted) {
            opError = QStringLiteral("Operation cancelled");
        }
#endif

        emit itemFinished(itemId, success, opError);

        // Update and prune queue entry
        {
            QMutexLocker lk2(&m_mutex);
            for (int i=0;i<m_queue.size();++i) if (m_queue[i].id == itemId) {
                if (!success) m_queue[i].status = aborted ? "Cancelled" : "Failed"; else m_queue[i].status = "Completed";
                m_queue.removeAt(i);
                break;
            }
            emit queueChanged();
        }
    });
    m_watcher.setFuture(m_future);
}

QString FileOpsQueue::uniqueNameInDir(const QString& dir, const QString& baseName)
{
    QString name = baseName;
    QString path = QDir(dir).filePath(name);
    int i=2;
    while (FileUtils::pathExists(path)) {
        QString stem = QFileInfo(baseName).completeBaseName();
        QString ext = QFileInfo(baseName).suffix();
        if (ext.isEmpty()) name = QString("%1 (%2)").arg(stem).arg(i++);
        else name = QString("%1 (%2).%3").arg(stem).arg(i++).arg(ext);
        path = QDir(dir).filePath(name);
    }
    return path;
}

bool FileOpsQueue::copyFileWithProgress(const QString& src, const QString& dst, std::atomic_bool& cancel,
                                        std::function<void(qint64,qint64)> onProgress, QString* errorOut)
{
    QFile in(src); QFile out(dst);
    if (!in.open(QIODevice::ReadOnly)) { if (errorOut) *errorOut = QObject::tr("Failed to open %1").arg(src); return false; }
    QDir().mkpath(QFileInfo(dst).absolutePath());
    if (!out.open(QIODevice::WriteOnly)) { if (errorOut) *errorOut = QObject::tr("Failed to write %1").arg(dst); return false; }
    const qint64 total = in.size();
    qint64 copied = 0;
    QByteArray buf; buf.resize(4*1024*1024);
    while (!in.atEnd()) {
        if (cancel.load()) { out.close(); out.remove(); return false; }
        qint64 r = in.read(buf.data(), buf.size());
        if (r <= 0) break;
        qint64 w = out.write(buf.constData(), r);
        if (w != r) { if (errorOut) *errorOut = QObject::tr("Write error %1").arg(dst); out.close(); return false; }
        copied += w;
        if (onProgress) onProgress(copied, total);
    }
    out.flush(); out.close(); in.close();
    return true;
}

bool FileOpsQueue::copyRecursively(const QString& src, const QString& dstDir, std::atomic_bool& cancel,
                                   std::function<void(const QString&, int, int)> onFile, QString* errorOut)
{
    QFileInfo sfi(src);
    if (sfi.isDir()) {
        QDir dst(dstDir);
        if (!dst.exists()) QDir().mkpath(dstDir);
        QDir srcDir(src);
        QFileInfoList entries = srcDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        int total = entries.size(); int count=0;
        for (const QFileInfo &e : entries) {
            if (cancel.load()) return false;
            QString target = dst.filePath(e.fileName());
            if (e.isDir()) {
                if (!copyRecursively(e.absoluteFilePath(), target, cancel, onFile, errorOut)) return false;
            } else {
                if (!copyFileWithProgress(e.absoluteFilePath(), target, cancel, nullptr, errorOut)) return false;
            }
            if (onFile) onFile(e.absoluteFilePath(), ++count, total);
        }
        return true;
    } else {
        return copyFileWithProgress(src, QDir(dstDir).filePath(sfi.fileName()), cancel, nullptr, errorOut);
    }
}

bool FileOpsQueue::removeRecursively(const QString& path, std::atomic_bool& cancel)
{
    if (!FileUtils::pathExists(path)) return true;
    QFileInfo fi(path);
    if (fi.isDir()) {
        QDir dir(path);
        QFileInfoList list = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
        for (const QFileInfo &e : list) {
            if (cancel.load()) return false;
            if (!removeRecursively(e.absoluteFilePath(), cancel)) return false;
        }
        return dir.rmdir(path);
    } else {
        return QFile::remove(path);
    }
}
