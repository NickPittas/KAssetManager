#pragma once
#include <QObject>
#include <QStringList>

class Importer : public QObject {
    Q_OBJECT
public:
    explicit Importer(QObject* parent=nullptr);

    bool importPaths(const QStringList& paths);
    bool importFile(const QString& filePath, int parentFolderId = 0);
    bool importFolder(const QString& dirPath, int parentFolderId = 0);

    // Batch import with progress reporting
    void importFiles(const QStringList& filePaths, int parentFolderId);

    // Maintenance utilities
    int purgeMissingAssets();
    int purgeAutotestAssets();

signals:
    void importCompleted(int filesImported);
    void progressChanged(int current, int total);
    void importFinished();

private:
    static bool isMediaFile(const QString& path);
};

