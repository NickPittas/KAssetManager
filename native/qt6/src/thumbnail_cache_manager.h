#pragma once

#include <QObject>
#include <QImage>
#include <QPixmap>
#include <QString>
#include <QSize>
#include <QMutex>
#include <QHash>
#include <QDateTime>

/**
 * @brief Manages persistent thumbnail cache for Asset Manager
 * 
 * Stores generated thumbnails on disk to avoid regenerating them.
 * Cache structure: [cache_dir]/[hash]/[size]_[position].jpg
 * Metadata: [cache_dir]/[hash]/meta.json (stores source file info)
 */
class ThumbnailCacheManager : public QObject
{
    Q_OBJECT

public:
    static ThumbnailCacheManager& instance();

    // Cache operations
    QPixmap getCachedThumbnail(const QString& filePath, const QSize& size, qreal position = 0.0);
    // NOTE: This intentionally takes QImage so it can be called from worker threads.
    // QPixmap is GUI-thread-only on Windows and can hard-crash if used in background threads.
    bool storeThumbnail(const QString& filePath, const QSize& size, qreal position, const QImage& image);
    bool isCached(const QString& filePath, const QSize& size, qreal position = 0.0) const;
    bool isOutdated(const QString& filePath) const;
    
    // Cache management
    void clearCache();
    void clearCacheForFile(const QString& filePath);
    qint64 getCacheSize() const;
    int getCachedFileCount() const;
    
    // Settings
    QString getCacheDirectory() const;
    void setCacheDirectory(const QString& dir);
    
    QSize getThumbnailSize() const;
    void setThumbnailSize(const QSize& size);
    
    // Load/save settings from QSettings
    void loadSettings();
    void saveSettings();

signals:
    void cacheCleared();
    void thumbnailStored(const QString& filePath, const QSize& size, qreal position);

private:
    explicit ThumbnailCacheManager(QObject* parent = nullptr);
    ~ThumbnailCacheManager() = default;
    ThumbnailCacheManager(const ThumbnailCacheManager&) = delete;
    ThumbnailCacheManager& operator=(const ThumbnailCacheManager&) = delete;

    // Helper methods
    QString hashFilePath(const QString& filePath) const;
    QString getCachePath(const QString& filePath, const QSize& size, qreal position) const;
    QString getMetadataPath(const QString& filePath) const;
    
    struct FileMetadata {
        QString filePath;
        qint64 fileSize = 0;
        QDateTime lastModified;
        
        bool isValid() const { return !filePath.isEmpty(); }
    };
    
    FileMetadata loadMetadata(const QString& filePath) const;
    bool saveMetadata(const QString& filePath, const FileMetadata& meta);
    FileMetadata getFileMetadata(const QString& filePath) const;

    mutable QMutex m_mutex;
    QString m_cacheDirectory;
    QSize m_thumbnailSize;
};

