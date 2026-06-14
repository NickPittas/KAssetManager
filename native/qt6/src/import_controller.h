#pragma once

#include <QObject>
#include <QStringList>

class DbWorker;

class ImportController : public QObject {
    Q_OBJECT
public:
    enum class DatabaseKind {
        AssetLibrary,
        ProjectManager
    };
    Q_ENUM(DatabaseKind)

    explicit ImportController(QObject* parent = nullptr);
    ~ImportController() override;

    bool start(DatabaseKind databaseKind, const QString& dbFilePath);
    void stop();
    bool isRunning() const;
    bool hasActiveImport() const { return m_activeJobId != 0; }

    bool importToAssetLibrary(const QStringList& filePaths,
                              const QStringList& folderPaths,
                              int targetFolderId,
                              bool sequenceDetectionEnabled = true);
    bool importFolderContentsToProject(const QString& dirPath,
                                       int targetFolderId,
                                       bool sequenceDetectionEnabled = true);

signals:
    void progressChanged(int current, int total);
    void currentFileChanged(const QString& fileName);
    void currentFolderChanged(const QString& folderName);
    void importFinished(int filesImported, const QList<int>& changedFolderIds);
    void importFailed(const QString& error);

private slots:
    void onJobFinished(int jobId, const QVariant& result);
    void onJobFailed(int jobId, const QString& error);

private:
    bool submitImport(const QStringList& filePaths,
                      const QStringList& folderPaths,
                      int targetFolderId,
                      bool importFolderContents,
                      bool sequenceDetectionEnabled);

    DbWorker* m_worker = nullptr;
    DatabaseKind m_databaseKind = DatabaseKind::AssetLibrary;
    int m_activeJobId = 0;
};
