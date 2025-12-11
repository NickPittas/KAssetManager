#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

/**
 * @brief Worker class for importing project assets in a background thread.
 * 
 * This worker creates its own database connection to avoid SQLite thread issues.
 * Progress is reported via signals to keep the UI responsive.
 * 
 * Note: Import is 1:1 - every file becomes one database row. Sequence detection
 * and grouping happens at display time via ProjectSequenceGroupingProxyModel.
 */
class ProjectImportWorker : public QObject {
    Q_OBJECT
public:
    explicit ProjectImportWorker(QObject* parent = nullptr);
    ~ProjectImportWorker();

    /**
     * @brief Set the database path and project info before starting.
     */
    void setDatabasePath(const QString& dbPath);
    void setImportInfo(int projectId, int targetFolderId, const QString& sourcePath);

public slots:
    /**
     * @brief Run the import (called via QMetaObject::invokeMethod from main thread).
     */
    void doImport();

    /**
     * @brief Request cancellation (thread-safe).
     */
    void requestCancel();

signals:
    void progressChanged(int current, int total);
    void currentFileChanged(const QString& fileName);
    void currentFolderChanged(const QString& folderName);
    void importFinished(bool success, const QString& errorMessage);

private:
    bool openDatabase();
    void closeDatabase();
    
    QString m_dbPath;
    int m_projectId = 0;
    int m_targetFolderId = 0;
    QString m_sourcePath;
    
    QSqlDatabase m_db;
    QString m_connectionName;
    
    std::atomic<bool> m_cancelRequested{false};
};

/**
 * @brief Controller class to manage the import worker thread.
 */
class ProjectImportController : public QObject {
    Q_OBJECT
public:
    explicit ProjectImportController(QObject* parent = nullptr);
    ~ProjectImportController();

    /**
     * @brief Start an asynchronous import.
     * Import is 1:1 - every file becomes one database row.
     */
    void startImport(const QString& dbPath, int projectId, int targetFolderId, 
                     const QString& sourcePath);

    /**
     * @brief Cancel the current import.
     */
    void cancelImport();

    bool isRunning() const { return m_running; }

signals:
    void progressChanged(int current, int total);
    void currentFileChanged(const QString& fileName);
    void currentFolderChanged(const QString& folderName);
    void importFinished(bool success, const QString& errorMessage);

private:
    QThread* m_thread = nullptr;
    ProjectImportWorker* m_worker = nullptr;
    bool m_running = false;
};
