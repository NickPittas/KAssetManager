#ifndef DATABASE_HEALTH_DIALOG_H
#define DATABASE_HEALTH_DIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QTreeWidget>
#include "database_health_agent.h"

class DatabaseHealthDialog : public QDialog {
    Q_OBJECT

public:
    explicit DatabaseHealthDialog(QWidget* parent = nullptr);
    ~DatabaseHealthDialog() = default;

private slots:
    void runHealthCheck();
    void performVacuum();
    void performReindex();
    void fixOrphanedRecords();
    void updateMissingFiles();
    
    void onHealthCheckStarted();
    void onHealthCheckProgress(int current, int total, const QString& message);
    void onHealthCheckCompleted(const QVector<HealthCheckResult>& results);
    void onMaintenanceStarted(const QString& operation);
    void onMaintenanceProgress(int percent);
    void onMaintenanceCompleted(bool success, const QString& message);

private:
    void setupUI();
    void updateStatistics();
    void displayResults(const QVector<HealthCheckResult>& results);
    QString formatFileSize(qint64 bytes) const;
    QIcon getSeverityIcon(HealthCheckResult::Severity severity) const;
    void setActionsEnabled(bool enabled);
    
    // UI components
    QVBoxLayout* m_mainLayout;
    
    // Statistics section
    QGroupBox* m_statsGroup;
    QLabel* m_dbSizeLabel;
    QLabel* m_assetCountLabel;
    QLabel* m_fragmentationLabel;
    QLabel* m_lastVacuumLabel;
    
    // Health check results
    QGroupBox* m_resultsGroup;
    QTreeWidget* m_resultsTree;
    QPushButton* m_runCheckButton;
    QProgressBar* m_progressBar;
    QLabel* m_progressLabel;
    
    // Maintenance actions
    QGroupBox* m_actionsGroup;
    QPushButton* m_vacuumButton;
    QPushButton* m_reindexButton;
    QPushButton* m_fixOrphansButton;
    QPushButton* m_updateMissingButton;
    
    // Status
    QLabel* m_statusLabel;
    QPushButton* m_closeButton;
    
    // Data
    DatabaseStats m_stats;
    QVector<HealthCheckResult> m_lastResults;
    bool m_checkRunning;
    bool m_maintenanceRunning;
};

#endif // DATABASE_HEALTH_DIALOG_H

