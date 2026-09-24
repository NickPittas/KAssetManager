#include "file_utils.h"

#include <QDir>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>

namespace FileUtils {

namespace {

QString storageFingerprint(const QStorageInfo& storage)
{
    if (!storage.isValid())
        return QString();

    const QByteArray device = storage.device();
    QString fingerprint = storage.rootPath();
    if (!device.isEmpty()) {
        fingerprint += QLatin1Char('|');
        fingerprint += QString::fromLatin1(device.toHex());
    }
    return fingerprint;
}

QString canonicalDirectoryPath(const QFileInfo& info)
{
    QString path = info.canonicalFilePath();
    if (path.isEmpty())
        path = info.absoluteFilePath();
    return path;
}

} // namespace

PathAvailabilityResult checkPathAvailability(
    const QString& path,
    PathAvailabilityMode mode,
    const QString& expectedStorageFingerprint)
{
    PathAvailabilityResult result;

    if (path.isEmpty()) {
        result.failure = PathAvailabilityFailure::EmptyPath;
        result.message = QStringLiteral("Path is empty");
        return result;
    }

    const QFileInfo inputInfo(path);
    const QString absoluteInput = inputInfo.absoluteFilePath();

    if (!inputInfo.exists()) {
        result.failure = PathAvailabilityFailure::Missing;
        result.normalizedPath = absoluteInput;
        result.directoryPath = absoluteInput;
        result.message = QStringLiteral("Path does not exist: %1").arg(absoluteInput);
        return result;
    }

    QString targetDir;
    if (mode == PathAvailabilityMode::FileOrContainingDirectory && inputInfo.isFile()) {
        targetDir = inputInfo.absolutePath();
    } else if (inputInfo.isDir()) {
        targetDir = path;
    } else {
        result.failure = PathAvailabilityFailure::NotDirectory;
        result.normalizedPath = canonicalDirectoryPath(inputInfo);
        result.directoryPath = result.normalizedPath;
        result.message = QStringLiteral("Path is not a directory: %1").arg(absoluteInput);
        return result;
    }

    const QFileInfo dirInfo(targetDir);
    const QString canonicalDir = canonicalDirectoryPath(dirInfo);
    result.normalizedPath = canonicalDir;
    result.directoryPath = canonicalDir;

    const QStorageInfo storage(canonicalDir);
    if (!storage.isValid()) {
        result.failure = PathAvailabilityFailure::StorageInvalid;
        result.message = QStringLiteral("Storage is invalid for: %1").arg(canonicalDir);
        return result;
    }

    if (!storage.isReady()) {
        result.failure = PathAvailabilityFailure::StorageNotReady;
        result.message = QStringLiteral("Storage is not ready: %1").arg(canonicalDir);
        return result;
    }

    if (!dirInfo.isReadable()) {
        result.failure = PathAvailabilityFailure::NotReadable;
        result.message = QStringLiteral("Directory is not readable: %1").arg(canonicalDir);
        return result;
    }

#if defined(Q_OS_UNIX)
    if (!dirInfo.isExecutable()) {
        result.failure = PathAvailabilityFailure::NotSearchable;
        result.message = QStringLiteral("Directory is not searchable: %1").arg(canonicalDir);
        return result;
    }
#endif

    result.storageFingerprint = storageFingerprint(storage);

    if (!expectedStorageFingerprint.isEmpty() && expectedStorageFingerprint != result.storageFingerprint) {
        result.failure = PathAvailabilityFailure::StorageFingerprintMismatch;
        result.message = QStringLiteral("Storage fingerprint mismatch for %1 (expected %2, current %3)")
                              .arg(canonicalDir, expectedStorageFingerprint, result.storageFingerprint);
        return result;
    }

    result.available = true;
    result.failure = PathAvailabilityFailure::None;
    result.message = QStringLiteral("Available: %1").arg(canonicalDir);
    return result;
}

QString firstAvailableDirectory(const QString& preferredPath)
{
    QStringList candidates;
    if (!preferredPath.isEmpty())
        candidates << preferredPath;
    candidates << QDir::homePath()
               << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               << QDir::tempPath();
    candidates.removeDuplicates();

    for (const QString& candidate : candidates) {
        if (candidate.isEmpty())
            continue;
        const PathAvailabilityResult result = checkPathAvailability(candidate);
        if (result.available)
            return result.normalizedPath;
    }

    return QString();
}

bool isPreviewableSuffix(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kImageSuffixes = {
        "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "gif",
        "webp", "heic", "heif", "avif", "psd", "exr", "hdr", "pfm", "dpx"
    };
    static const QSet<QString> kVideoSuffixes = {
        "mov", "qt", "mp4", "m4v", "mxf", "mkv", "avi", "asf",
        "wmv", "webm", "mpg", "mpeg", "m2v", "m2ts", "mts",
        "ogv", "flv", "f4v", "3gp", "3g2", "y4m"
    };
    const QString lower = suffix.toLower();
    return kImageSuffixes.contains(lower) || kVideoSuffixes.contains(lower);
}

bool isImageFile(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kImageSuffixes = {
        "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "gif",
        "webp", "heic", "heif", "avif", "psd", "exr", "hdr", "pfm", "dpx",
        "ico", "svg", "cin" // Additional formats from sequence_grouping_proxy_model
    };
    const QString lower = suffix.toLower();
    return kImageSuffixes.contains(lower);
}

bool isVideoFile(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kVideoSuffixes = {
        "mov", "qt", "mp4", "m4v", "mxf", "mkv", "avi", "asf",
        "wmv", "webm", "mpg", "mpeg", "m2v", "m2ts", "mts",
        "ogv", "flv", "f4v", "3gp", "3g2", "y4m"
    };
    const QString lower = suffix.toLower();
    return kVideoSuffixes.contains(lower);
}

} // namespace FileUtils
