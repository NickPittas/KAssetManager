#ifndef DATABASE_HEALTH_AGENT_H
#define DATABASE_HEALTH_AGENT_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>

// Health check result structure
struct HealthCheckResult {
    enum Severity {
        Info,
        Warning,
        Critical
    };
    
    QString category;
    QString message;
    Severity severity;
    QString recommendation;
    bool autoFixable;
    
    HealthCheckResult(const QString& cat, const QString& msg, Severity sev, const QString& rec = QString(), bool fixable = false)
        : category(cat), message(msg), severity(sev), recommendation(rec), autoFixable(fixable) {}
};

// Database statistics structure
struct DatabaseStats {
    qint64 totalSize = 0;           // Total database file size in bytes
    qint64 pageSize = 0;            // Page size in bytes
    qint64 pageCount = 0;           // Total number of pages
    qint64 freePageCount = 0;       // Number of free pages
    qint64 fragmentationPercent = 0; // Fragmentation percentage
    int assetCount = 0;             // Total number of assets
    int folderCount = 0;            // Total number of folders
    int tagCount = 0;               // Total number of tags
    int orphanedAssets = 0;         // Assets with invalid folder references
    int missingFiles = 0;           // Assets pointing to non-existent files
    QDateTime lastVacuum;           // Last VACUUM operation timestamp
    QDateTime lastIntegrityCheck;   // Last integrity check timestamp
};

class DatabaseHealthAgent : public QObject {
    Q_OBJECT

public:
    static DatabaseHealthAgent& instance();
    
    // Run comprehensive health check
    QVector<HealthCheckResult> runHealthCheck();
    
    // Get database statistics
    DatabaseStats getDatabaseStats();
    
    // Individual check methods
    QVector<HealthCheckResult> checkOrphanedRecords();
    QVector<HealthCheckResult> checkMissingFiles();
    QVector<HealthCheckResult> checkFragmentation();
    QVector<HealthCheckResult> checkIntegrity();
    QVector<HealthCheckResult> checkIndexes();
    
    // Maintenance operations
    bool performVacuum();
    bool rebuildIndexes();
    bool fixOrphanedRecords();
    bool updateMissingFileStatus();
    
    // Recommendations
    bool shouldVacuum() const;
    QString getVacuumRecommendation() const;
    
signals:
    void healthCheckStarted();
    void healthCheckProgress(int current, int total, const QString& message);
    void healthCheckCompleted(const QVector<HealthCheckResult>& results);
    void maintenanceStarted(const QString& operation);
    void maintenanceProgress(int percent);
    void maintenanceCompleted(bool success, const QString& message);

private:
    DatabaseHealthAgent();
    ~DatabaseHealthAgent() = default;
    DatabaseHealthAgent(const DatabaseHealthAgent&) = delete;
    DatabaseHealthAgent& operator=(const DatabaseHealthAgent&) = delete;
    
    // Helper methods
    qint64 getDatabaseFileSize() const;
    qint64 getFragmentation() const;
    QDateTime getLastVacuumTime() const;
    void saveMaintenanceTimestamp(const QString& operation);
};

#endif // DATABASE_HEALTH_AGENT_H

