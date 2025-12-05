#include "thumbnail_cache_manager.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QMutexLocker>
#include <QDebug>

ThumbnailCacheManager& ThumbnailCacheManager::instance()
{
    static ThumbnailCacheManager s_instance;
    return s_instance;
}

ThumbnailCacheManager::ThumbnailCacheManager(QObject* parent)
    : QObject(parent)
    , m_thumbnailSize(256, 256)
{
    loadSettings();
}

void ThumbnailCacheManager::loadSettings()
{
    QSettings s("AugmentCode", "KAssetManager");
    
    // Default cache directory: next to database in AppData
    QString defaultCacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/thumbnail_cache";
    m_cacheDirectory = s.value("ThumbnailCache/Directory", defaultCacheDir).toString();
    
    int width = s.value("ThumbnailCache/Width", 256).toInt();
    int height = s.value("ThumbnailCache/Height", 256).toInt();
    m_thumbnailSize = QSize(width, height);
    
    // Ensure cache directory exists
    QDir().mkpath(m_cacheDirectory);
    
    qDebug() << "[ThumbnailCache] Loaded settings - dir:" << m_cacheDirectory << "size:" << m_thumbnailSize;
}

void ThumbnailCacheManager::saveSettings()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("ThumbnailCache/Directory", m_cacheDirectory);
    s.setValue("ThumbnailCache/Width", m_thumbnailSize.width());
    s.setValue("ThumbnailCache/Height", m_thumbnailSize.height());
    s.sync();
}

QString ThumbnailCacheManager::hashFilePath(const QString& filePath) const
{
    // Use MD5 hash of absolute file path as cache key
    QByteArray hash = QCryptographicHash::hash(
        QFileInfo(filePath).absoluteFilePath().toUtf8(),
        QCryptographicHash::Md5
    );
    return QString::fromLatin1(hash.toHex());
}

QString ThumbnailCacheManager::getCachePath(const QString& filePath, const QSize& size, qreal position) const
{
    QString hash = hashFilePath(filePath);
    QString cacheDir = m_cacheDirectory + "/" + hash;
    
    // Filename: [width]x[height]_[position].jpg
    // Position is stored as integer (0-1000) to avoid floating point issues
    int posInt = static_cast<int>(position * 1000.0);
    QString filename = QString("%1x%2_%3.jpg").arg(size.width()).arg(size.height()).arg(posInt);
    
    return cacheDir + "/" + filename;
}

QString ThumbnailCacheManager::getMetadataPath(const QString& filePath) const
{
    QString hash = hashFilePath(filePath);
    return m_cacheDirectory + "/" + hash + "/meta.json";
}

ThumbnailCacheManager::FileMetadata ThumbnailCacheManager::getFileMetadata(const QString& filePath) const
{
    QFileInfo info(filePath);
    if (!info.exists()) {
        return FileMetadata();
    }
    
    FileMetadata meta;
    meta.filePath = info.absoluteFilePath();
    meta.fileSize = info.size();
    meta.lastModified = info.lastModified();
    return meta;
}

ThumbnailCacheManager::FileMetadata ThumbnailCacheManager::loadMetadata(const QString& filePath) const
{
    QString metaPath = getMetadataPath(filePath);
    QFile file(metaPath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return FileMetadata();
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return FileMetadata();
    }
    
    QJsonObject obj = doc.object();
    FileMetadata meta;
    meta.filePath = obj["filePath"].toString();
    meta.fileSize = obj["fileSize"].toVariant().toLongLong();
    meta.lastModified = QDateTime::fromString(obj["lastModified"].toString(), Qt::ISODate);
    
    return meta;
}

bool ThumbnailCacheManager::saveMetadata(const QString& filePath, const FileMetadata& meta)
{
    QString metaPath = getMetadataPath(filePath);
    QDir().mkpath(QFileInfo(metaPath).absolutePath());
    
    QJsonObject obj;
    obj["filePath"] = meta.filePath;
    obj["fileSize"] = QString::number(meta.fileSize);
    obj["lastModified"] = meta.lastModified.toString(Qt::ISODate);
    
    QFile file(metaPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "[ThumbnailCache] Failed to write metadata:" << metaPath;
        return false;
    }
    
    file.write(QJsonDocument(obj).toJson());
    return true;
}

QPixmap ThumbnailCacheManager::getCachedThumbnail(const QString& filePath, const QSize& size, qreal position)
{
    QMutexLocker locker(&m_mutex);
    
    QString cachePath = getCachePath(filePath, size, position);
    if (!QFile::exists(cachePath)) {
        return QPixmap();
    }
    
    // Check if cache is outdated
    if (isOutdated(filePath)) {
        return QPixmap();
    }
    
    QPixmap pixmap(cachePath);
    return pixmap;
}

bool ThumbnailCacheManager::storeThumbnail(const QString& filePath, const QSize& size, qreal position, const QPixmap& pixmap)
{
    QMutexLocker locker(&m_mutex);

    if (pixmap.isNull()) {
        return false;
    }

    QString cachePath = getCachePath(filePath, size, position);
    QDir().mkpath(QFileInfo(cachePath).absolutePath());

#ifndef NDEBUG
    // Debug logging for storage (only in debug builds)
    QString absPath = QFileInfo(filePath).absoluteFilePath();
    QString hash = hashFilePath(filePath);
    qDebug() << "[ThumbnailCache] storeThumbnail:";
    qDebug() << "  Input path:" << filePath;
    qDebug() << "  Absolute path:" << absPath;
    qDebug() << "  Hash:" << hash;
    qDebug() << "  Cache path:" << cachePath;
#endif

    // Save thumbnail as JPEG (good compression for photos/videos)
    if (!pixmap.save(cachePath, "JPG", 90)) {
        qWarning() << "[ThumbnailCache] Failed to save thumbnail:" << cachePath;
        return false;
    }

    // Save metadata on first thumbnail for this file
    FileMetadata meta = getFileMetadata(filePath);
    if (meta.isValid()) {
        saveMetadata(filePath, meta);
#ifndef NDEBUG
        qDebug() << "[ThumbnailCache] Saved metadata - size:" << meta.fileSize << "modified:" << meta.lastModified;
#endif
    }

    emit thumbnailStored(filePath, size, position);
    return true;
}

bool ThumbnailCacheManager::isCached(const QString& filePath, const QSize& size, qreal position) const
{
    QMutexLocker locker(&m_mutex);
    QString cachePath = getCachePath(filePath, size, position);
    bool exists = QFile::exists(cachePath);
    bool outdated = isOutdated(filePath);

#ifndef NDEBUG
    // Debug logging for cache lookups (only in debug builds - very verbose)
    static int logCounter = 0;
    if (++logCounter % 100 == 0) { // Rate limit: log every 100th call
        QString absPath = QFileInfo(filePath).absoluteFilePath();
        QString hash = hashFilePath(filePath);
        qDebug() << "[ThumbnailCache] isCached check (sampled):";
        qDebug() << "  Input path:" << filePath;
        qDebug() << "  Exists:" << exists << "Outdated:" << outdated;
    }
#endif

    return exists && !outdated;
}

bool ThumbnailCacheManager::isOutdated(const QString& filePath) const
{
    // Check if source file has been modified since cache was created
    FileMetadata cached = loadMetadata(filePath);
    if (!cached.isValid()) {
        qDebug() << "[ThumbnailCache] isOutdated: No valid metadata for" << filePath;
        return true; // No metadata = outdated
    }

    FileMetadata current = getFileMetadata(filePath);
    if (!current.isValid()) {
        qDebug() << "[ThumbnailCache] isOutdated: Source file doesn't exist:" << filePath;
        return true; // Source file doesn't exist
    }

    // Compare file size and modification time
    // NOTE: Compare modification time at SECOND precision (ignore milliseconds)
    // because different file systems and Qt operations may have different millisecond precision
    bool sizeChanged = (cached.fileSize != current.fileSize);
    bool timeChanged = (cached.lastModified.toSecsSinceEpoch() != current.lastModified.toSecsSinceEpoch());

    if (sizeChanged || timeChanged) {
        qDebug() << "[ThumbnailCache] isOutdated: File changed";
        qDebug() << "  Cached size:" << cached.fileSize << "Current size:" << current.fileSize;
        qDebug() << "  Cached time:" << cached.lastModified << "Current time:" << current.lastModified;
        qDebug() << "  Cached time (secs):" << cached.lastModified.toSecsSinceEpoch() << "Current time (secs):" << current.lastModified.toSecsSinceEpoch();
    }

    return sizeChanged || timeChanged;
}

void ThumbnailCacheManager::clearCache()
{
    QMutexLocker locker(&m_mutex);

    QDir cacheDir(m_cacheDirectory);
    if (cacheDir.exists()) {
        cacheDir.removeRecursively();
        QDir().mkpath(m_cacheDirectory);
    }

    emit cacheCleared();
    qDebug() << "[ThumbnailCache] Cache cleared";
}

void ThumbnailCacheManager::clearCacheForFile(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);

    QString hash = hashFilePath(filePath);
    QString fileCache = m_cacheDirectory + "/" + hash;

    QDir dir(fileCache);
    if (dir.exists()) {
        dir.removeRecursively();
    }
}

qint64 ThumbnailCacheManager::getCacheSize() const
{
    QMutexLocker locker(&m_mutex);

    qint64 totalSize = 0;
    QDir cacheDir(m_cacheDirectory);

    QFileInfoList entries = cacheDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& dirInfo : entries) {
        QDir subDir(dirInfo.absoluteFilePath());
        QFileInfoList files = subDir.entryInfoList(QDir::Files);
        for (const QFileInfo& fileInfo : files) {
            totalSize += fileInfo.size();
        }
    }

    return totalSize;
}

int ThumbnailCacheManager::getCachedFileCount() const
{
    QMutexLocker locker(&m_mutex);

    QDir cacheDir(m_cacheDirectory);
    QFileInfoList entries = cacheDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    return entries.size();
}

QString ThumbnailCacheManager::getCacheDirectory() const
{
    QMutexLocker locker(&m_mutex);
    return m_cacheDirectory;
}

void ThumbnailCacheManager::setCacheDirectory(const QString& dir)
{
    QMutexLocker locker(&m_mutex);
    m_cacheDirectory = dir;
    QDir().mkpath(m_cacheDirectory);
    saveSettings();
}

QSize ThumbnailCacheManager::getThumbnailSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_thumbnailSize;
}

void ThumbnailCacheManager::setThumbnailSize(const QSize& size)
{
    QMutexLocker locker(&m_mutex);
    m_thumbnailSize = size;
    saveSettings();
}

