#pragma once
#include <QObject>
#include <QStringList>
#include <QtQml/qqml.h>

class Importer : public QObject {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit Importer(QObject* parent=nullptr);

    Q_INVOKABLE bool importPaths(const QStringList& paths);
    Q_INVOKABLE bool importFile(const QString& filePath, int parentFolderId = 0);
    Q_INVOKABLE bool importFolder(const QString& dirPath, int parentFolderId = 0);

    // Maintenance utilities
    Q_INVOKABLE int purgeMissingAssets();
    Q_INVOKABLE int purgeAutotestAssets();

signals:
    void importCompleted(int filesImported);

private:
    static bool isMediaFile(const QString& path);
};

