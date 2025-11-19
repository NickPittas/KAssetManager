#include "sequence_grouping_proxy_model.h"
#include "sequence_detector.h"
#include "file_utils.h"
#include <QDebug>



SequenceGroupingProxyModel::SequenceGroupingProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    connect(&m_buildWatcher, &QFutureWatcher<BuildResult>::finished, this, [this]() {
        BuildResult result = m_buildWatcher.result();
        if (result.dirPath == m_requestedDir) {
            applyBuildResult(std::move(result));
        }

        if (!m_pendingDir.isEmpty()) {
            const QString nextDir = m_pendingDir;
            m_pendingDir.clear();
            startBuild(nextDir);
        } else {
            m_activeDir.clear();
        }
    });
}

void SequenceGroupingProxyModel::setGroupingEnabled(bool on) {
    if (m_enabled == on) return;
    m_enabled = on;
    invalidateFilter();
}

bool SequenceGroupingProxyModel::groupingEnabled() const {
    return m_enabled;
}

void SequenceGroupingProxyModel::setHideFolders(bool hide) {
    if (m_hideFolders == hide) return;
    m_hideFolders = hide;
    invalidateFilter();
}

bool SequenceGroupingProxyModel::hideFolders() const {
    return m_hideFolders;
}

void SequenceGroupingProxyModel::rebuildForRoot(const QString& dirPath) {
    m_requestedDir = dirPath;
    m_hidden.clear();
    m_infoByRepr.clear();
    m_keyByRepr.clear();
    invalidateFilter();

    if (!m_enabled || dirPath.isEmpty()) {
        m_pendingDir.clear();
        return;
    }

    if (m_buildWatcher.isRunning()) {
        m_pendingDir = dirPath;
        return;
    }

    startBuild(dirPath);
}

bool SequenceGroupingProxyModel::isRepresentativeProxyIndex(const QModelIndex& proxyIdx) const {
    if (!proxyIdx.isValid()) return false;
    QModelIndex src = mapToSource(proxyIdx);
    auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return false;
    const QString path = fs->filePath(src);
    return m_infoByRepr.contains(path);
}

SequenceGroupingProxyModel::Info SequenceGroupingProxyModel::infoForProxyIndex(const QModelIndex& proxyIdx) const {
    Info i; if (!proxyIdx.isValid()) return i;
    QModelIndex src = mapToSource(proxyIdx);
    auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return i;
    const QString path = fs->filePath(src);
    return m_infoByRepr.value(path);
}

bool SequenceGroupingProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const {
    auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return true;
    QModelIndex idx = fs->index(source_row, 0, source_parent);
    if (!idx.isValid()) return true;

    const bool isDir = fs->isDir(idx);
    const QString path = fs->filePath(idx);

    // Check if we should hide folders - this is independent of sequence grouping
    if (isDir) {
        return !m_hideFolders; // Hide folders if flag is set, show if not
    }

    // For files: check sequence grouping (only if enabled)
    if (!m_enabled) {
        // Sequence grouping is disabled, show all files
        return true;
    }

    // Sequence grouping is enabled
    // Show files that are NOT in the hidden set (i.e., show representatives and non-sequence files)
    return !m_hidden.contains(path);
}

QVariant SequenceGroupingProxyModel::data(const QModelIndex &proxyIndex, int role) const {
    if (!m_enabled || role != Qt::DisplayRole) return QSortFilterProxyModel::data(proxyIndex, role);
    if (!isRepresentativeProxyIndex(proxyIndex)) return QSortFilterProxyModel::data(proxyIndex, role);
    Info info = infoForProxyIndex(proxyIndex);
    // Compute padded range string (keep width of start frame digits)
    int pad = QString::number(info.start).size();
    QString startStr = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
    QString endStr = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
    return QString("%1.[%2-%3].%4").arg(info.base, startStr, endStr, info.ext);
}

bool SequenceGroupingProxyModel::lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const {
    auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
    if (!fs) return QSortFilterProxyModel::lessThan(source_left, source_right);
    const bool leftIsDir = fs->isDir(source_left);
    const bool rightIsDir = fs->isDir(source_right);
    if (leftIsDir != rightIsDir) {
        // Folders-first invariant: folders should be "less than" files to appear first
        // In ascending order, "less than" means "comes before"
        // Return true if left is a folder (should come first)
        return leftIsDir;
    }
    // Same type (both folders or both files): defer to default comparison by name
    return QSortFilterProxyModel::lessThan(source_left, source_right);
}

void SequenceGroupingProxyModel::sort(int column, Qt::SortOrder order) {
    m_sortOrder = order;
    QSortFilterProxyModel::sort(column, order);
}

void SequenceGroupingProxyModel::startBuild(const QString& dirPath) {
    m_activeDir = dirPath;
    QFuture<BuildResult> future = QtConcurrent::run([dirPath]() -> BuildResult {
        return buildSequences(dirPath);
    });
    m_buildWatcher.setFuture(future);
}

void SequenceGroupingProxyModel::applyBuildResult(BuildResult&& result) {
    m_hidden = std::move(result.hidden);
    m_infoByRepr = std::move(result.infoByRepr);
    m_keyByRepr = std::move(result.keyByRepr);
    invalidateFilter();
}

SequenceGroupingProxyModel::BuildResult SequenceGroupingProxyModel::buildSequences(const QString& dirPath) {
    BuildResult out;
    out.dirPath = dirPath;
    if (dirPath.isEmpty()) return out;

    QDir d(dirPath);
    const auto files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);

    QHash<QString, QFileInfo> headRepr;
    QHash<QString, int> headCount;
    for (const QFileInfo &fi : files) {
        const QString name = fi.fileName();
        auto m = SequenceDetector::mainPattern().match(name);
        if (!m.hasMatch()) continue;
        const QString base = m.captured(1);
        const QString ext = m.captured(4).toLower();
        if (!FileUtils::isImageFile(ext)) continue;
        const QString key = fi.absolutePath() + "|" + base + "|" + ext;
        headCount[key] += 1;
        if (!headRepr.contains(key)) headRepr.insert(key, fi);
    }

    for (auto it = headRepr.begin(); it != headRepr.end(); ++it) {
        const QString key = it.key();
        const int count = headCount.value(key, 0);
        if (count <= 1) continue;

        const QFileInfo repr = it.value();
        const QString name = repr.fileName();
        auto mm = SequenceDetector::mainPattern().match(name);
        const int pad = mm.captured(3).length();

        const int digitsStart = mm.capturedStart(3);
        const int digitsEnd = mm.capturedEnd(3);
        const QString pre = name.left(digitsStart);
        const QString post = name.mid(digitsEnd);
        QDir dir = repr.dir();
        auto existsFrame = [&](qint64 n)->bool {
            if (n < 0) return false;
            const QString digits = QString::number(n).rightJustified(pad, QLatin1Char('0'));
            return QFileInfo(dir.filePath(pre + digits + post)).exists();
        };

        const qint64 curN = mm.captured(3).toLongLong();
        qint64 low = -1, high = curN;
        while (high - low > 1) {
            qint64 mid = low + (high - low) / 2;
            if (existsFrame(mid)) high = mid; else low = mid;
        }
        const qint64 first = high;

        const qint64 START_HUGE = 10000000;
        qint64 lastKnownExist = curN;
        qint64 lastKnownNonExist = -1;
        qint64 probe = START_HUGE;
        while (probe > lastKnownExist) {
            if (existsFrame(probe)) { lastKnownExist = probe; break; }
            lastKnownNonExist = probe; probe /= 2;
        }
        if (lastKnownExist == curN) {
            qint64 up = std::max<qint64>(curN + 1, 2 * curN);
            for (int i = 0; i < 32; ++i) {
                if (!existsFrame(up)) { lastKnownNonExist = up; break; }
                if (up > 100000000) { lastKnownNonExist = up + 1; break; }
                up *= 2;
            }
            if (lastKnownNonExist < 0) lastKnownNonExist = curN + 1;
        } else {
            if (lastKnownNonExist < 0) lastKnownNonExist = lastKnownExist + 1;
        }
        qint64 lo = lastKnownExist, hi = lastKnownNonExist;
        if (hi <= lo) hi = lo + 1;
        while (hi - lo > 1) {
            qint64 mid = lo + (hi - lo) / 2;
            if (existsFrame(mid)) lo = mid; else hi = mid;
        }
        const qint64 last = lo;

        Info info;
        info.dir = repr.absolutePath();
        info.base = mm.captured(1);
        info.ext = mm.captured(4).toLower();
        info.start = (int)first;
        info.end = (int)last;
        info.count = count;
        info.reprPath = repr.absoluteFilePath();
        out.infoByRepr.insert(info.reprPath, info);
        out.keyByRepr.insert(info.reprPath, key);
    }

    for (const QFileInfo &fi : files) {
        const QString name = fi.fileName();
        auto m = SequenceDetector::mainPattern().match(name);
        if (!m.hasMatch()) continue;
        const QString base = m.captured(1);
        const QString ext = m.captured(4).toLower();
        if (!FileUtils::isImageFile(ext)) continue;
        const QString key = fi.absolutePath() + "|" + base + "|" + ext;
        const QString reprPath = headRepr.value(key).absoluteFilePath();
        if (!reprPath.isEmpty() && fi.absoluteFilePath() != reprPath && headCount.value(key,0) > 1) {
            out.hidden.insert(fi.absoluteFilePath());
        }
    }
    return out;
}
