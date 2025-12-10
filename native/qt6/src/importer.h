#pragma once
#include <QObject>
#include <QStringList>
#include "sequence_detector.h"

class IAssetDatabase;

class Importer : public QObject {
    Q_OBJECT
public:
    /**
     * @brief Construct an Importer with optional database injection.
     * @param parent Parent QObject
     * @param db Database to use for imports. If nullptr, uses DB::instance().
     * 
     * ⚠️ CRITICAL: For Asset Manager, pass nullptr or omit to use DB::instance().
     * For Project Manager, pass ProjectDB::instance() pointer.
     */
    explicit Importer(QObject* parent = nullptr, IAssetDatabase* db = nullptr);

    void setSequenceDetectionEnabled(bool enabled);
    bool sequenceDetectionEnabled() const { return m_sequenceDetectionEnabled; }

    bool importPaths(const QStringList& paths);
    bool importFile(const QString& filePath, int parentFolderId = 0);
    bool importFolder(const QString& dirPath, int parentFolderId = 0);
    
    /**
     * @brief Import folder contents directly into target folder without creating wrapper folder.
     * @param dirPath Path to directory whose contents should be imported
     * @param targetFolderId Target folder ID to import contents into
     * @return true if successful
     */
    bool importFolderContents(const QString& dirPath, int targetFolderId);

    // Batch import with progress reporting
    void importFiles(const QStringList& filePaths, int parentFolderId);

    // Maintenance utilities (only work with DB::instance(), not injected DB)
    int purgeMissingAssets();
    int purgeAutotestAssets();

signals:
    void importCompleted(int filesImported);
    void progressChanged(int current, int total);
    void currentFileChanged(const QString& fileName);
    void currentFolderChanged(const QString& folderName);
    void importFinished();

private:
    static bool isMediaFile(const QString& path);
    IAssetDatabase& m_db; // Reference to the database (either DB or ProjectDB)
    bool m_sequenceDetectionEnabled = true;
};
