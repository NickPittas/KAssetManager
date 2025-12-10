#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QString>

/**
 * @brief Abstract interface for asset database operations.
 * 
 * This interface enables dependency injection for the Importer class,
 * allowing it to work with both the main Asset Manager database (DB)
 * and the separate Project Manager database (ProjectDB).
 * 
 * ⚠️ CRITICAL: When implementing this interface, ensure all method
 * signatures match exactly. The Importer relies on these methods
 * for import operations.
 */
class IAssetDatabase {
public:
    virtual ~IAssetDatabase() = default;

    /**
     * @brief Get the underlying QSqlDatabase connection.
     * Used by Importer for transaction management.
     */
    virtual QSqlDatabase database() const = 0;

    /**
     * @brief Ensure root folder exists and return its ID.
     * Creates the root folder if it doesn't exist.
     * @return Root folder ID (always > 0 on success)
     */
    virtual int ensureRootFolder() = 0;

    /**
     * @brief Create a new folder under the given parent.
     * @param name Folder name
     * @param parentId Parent folder ID (use ensureRootFolder() for root)
     * @return New folder ID, or 0 on failure
     */
    virtual int createFolder(const QString& name, int parentId) = 0;

    /**
     * @brief Insert asset metadata without checksum computation.
     * Fast path for bulk imports - metadata only, no versioning, no signals.
     * @param filePath Absolute path to the asset file
     * @param folderId Target folder ID
     * @return Asset ID, or 0 on failure (e.g., duplicate)
     */
    virtual int insertAssetMetadataFast(const QString& filePath, int folderId) = 0;

    /**
     * @brief Insert or update an image sequence record.
     * Fast path for bulk imports - no signals emitted.
     * @param sequencePattern Pattern like "render.####.exr"
     * @param startFrame First frame number
     * @param endFrame Last frame number
     * @param frameCount Total frames (may differ from range if gaps)
     * @param firstFramePath Path to first frame file
     * @param folderId Target folder ID
     * @param hasGaps Whether sequence has missing frames
     * @param gapCount Number of gaps detected
     * @param version Version string extracted from filename (e.g., "v01")
     * @return Sequence asset ID, or 0 on failure
     */
    virtual int upsertSequenceInFolderFast(const QString& sequencePattern, 
                                           int startFrame, 
                                           int endFrame, 
                                           int frameCount, 
                                           const QString& firstFramePath, 
                                           int folderId, 
                                           bool hasGaps = false, 
                                           int gapCount = 0, 
                                           const QString& version = QString()) = 0;

    /**
     * @brief Emit signal to notify views that assets in a folder changed.
     * Called after bulk imports to trigger UI refresh.
     * @param folderId The folder whose assets changed
     */
    virtual void notifyAssetsChanged(int folderId) = 0;
};
