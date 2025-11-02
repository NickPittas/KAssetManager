#include "database_health_dialog.h"
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QDateTime>
#include <QStyle>

DatabaseHealthDialog::DatabaseHealthDialog(QWidget* parent)
    : QDialog(parent)
    , m_checkRunning(false)
    , m_maintenanceRunning(false)
{
    setWindowTitle("Database Health Check");
    resize(800, 600);
    
    setupUI();
    updateStatistics();
    
    // Connect to health agent signals
    DatabaseHealthAgent& agent = DatabaseHealthAgent::instance();
    connect(&agent, &DatabaseHealthAgent::healthCheckStarted, this, &DatabaseHealthDialog::onHealthCheckStarted);
    connect(&agent, &DatabaseHealthAgent::healthCheckProgress, this, &DatabaseHealthDialog::onHealthCheckProgress);
    connect(&agent, &DatabaseHealthAgent::healthCheckCompleted, this, &DatabaseHealthDialog::onHealthCheckCompleted);
    connect(&agent, &DatabaseHealthAgent::maintenanceStarted, this, &DatabaseHealthDialog::onMaintenanceStarted);
    connect(&agent, &DatabaseHealthAgent::maintenanceProgress, this, &DatabaseHealthDialog::onMaintenanceProgress);
    connect(&agent, &DatabaseHealthAgent::maintenanceCompleted, this, &DatabaseHealthDialog::onMaintenanceCompleted);
}

void DatabaseHealthDialog::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(12);
    
    // Statistics section
    m_statsGroup = new QGroupBox("Database Statistics", this);
    QVBoxLayout* statsLayout = new QVBoxLayout(m_statsGroup);
    
    m_dbSizeLabel = new QLabel(this);
    m_assetCountLabel = new QLabel(this);
    m_fragmentationLabel = new QLabel(this);
    m_lastVacuumLabel = new QLabel(this);
    
    statsLayout->addWidget(m_dbSizeLabel);
    statsLayout->addWidget(m_assetCountLabel);
    statsLayout->addWidget(m_fragmentationLabel);
    statsLayout->addWidget(m_lastVacuumLabel);
    
    m_mainLayout->addWidget(m_statsGroup);
    
    // Health check results section
    m_resultsGroup = new QGroupBox("Health Check Results", this);
    QVBoxLayout* resultsLayout = new QVBoxLayout(m_resultsGroup);
    
    m_resultsTree = new QTreeWidget(this);
    m_resultsTree->setHeaderLabels({"Category", "Status", "Details"});
    m_resultsTree->setColumnWidth(0, 150);
    m_resultsTree->setColumnWidth(1, 100);
    m_resultsTree->setAlternatingRowColors(true);
    m_resultsTree->setRootIsDecorated(false);
    
    resultsLayout->addWidget(m_resultsTree);
    
    QHBoxLayout* checkLayout = new QHBoxLayout();
    m_runCheckButton = new QPushButton("Run Health Check", this);
    m_runCheckButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    connect(m_runCheckButton, &QPushButton::clicked, this, &DatabaseHealthDialog::runHealthCheck);
    checkLayout->addWidget(m_runCheckButton);
    checkLayout->addStretch();
    
    resultsLayout->addLayout(checkLayout);
    
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    resultsLayout->addWidget(m_progressBar);
    
    m_progressLabel = new QLabel(this);
    m_progressLabel->setVisible(false);
    resultsLayout->addWidget(m_progressLabel);
    
    m_mainLayout->addWidget(m_resultsGroup);
    
    // Maintenance actions section
    m_actionsGroup = new QGroupBox("Maintenance Actions", this);
    QHBoxLayout* actionsLayout = new QHBoxLayout(m_actionsGroup);
    
    m_vacuumButton = new QPushButton("Optimize (VACUUM)", this);
    m_vacuumButton->setToolTip("Defragment database and reclaim unused space");
    connect(m_vacuumButton, &QPushButton::clicked, this, &DatabaseHealthDialog::performVacuum);
    actionsLayout->addWidget(m_vacuumButton);
    
    m_reindexButton = new QPushButton("Rebuild Indexes", this);
    m_reindexButton->setToolTip("Rebuild all database indexes for optimal performance");
    connect(m_reindexButton, &QPushButton::clicked, this, &DatabaseHealthDialog::performReindex);
    actionsLayout->addWidget(m_reindexButton);
    
    m_fixOrphansButton = new QPushButton("Fix Orphaned Records", this);
    m_fixOrphansButton->setToolTip("Move orphaned assets to root folder and clean up invalid references");
    connect(m_fixOrphansButton, &QPushButton::clicked, this, &DatabaseHealthDialog::fixOrphanedRecords);
    actionsLayout->addWidget(m_fixOrphansButton);
    
    m_updateMissingButton = new QPushButton("Check Missing Files", this);
    m_updateMissingButton->setToolTip("Scan all assets and identify missing files");
    connect(m_updateMissingButton, &QPushButton::clicked, this, &DatabaseHealthDialog::updateMissingFiles);
    actionsLayout->addWidget(m_updateMissingButton);
    
    m_mainLayout->addWidget(m_actionsGroup);
    
    // Status and close
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #666;");
    bottomLayout->addWidget(m_statusLabel);
    bottomLayout->addStretch();
    
    m_closeButton = new QPushButton("Close", this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    bottomLayout->addWidget(m_closeButton);
    
    m_mainLayout->addLayout(bottomLayout);
}

void DatabaseHealthDialog::updateStatistics() {
    m_stats = DatabaseHealthAgent::instance().getDatabaseStats();
    
    m_dbSizeLabel->setText(QString("Database Size: %1").arg(formatFileSize(m_stats.totalSize)));
    m_assetCountLabel->setText(QString("Total Assets: %1 | Folders: %2 | Tags: %3")
        .arg(m_stats.assetCount).arg(m_stats.folderCount).arg(m_stats.tagCount));
    
    QString fragColor = m_stats.fragmentationPercent > 20 ? "#d32f2f" : (m_stats.fragmentationPercent > 10 ? "#f57c00" : "#388e3c");
    m_fragmentationLabel->setText(QString("Fragmentation: <span style='color:%1;font-weight:bold;'>%2%</span>")
        .arg(fragColor).arg(m_stats.fragmentationPercent));
    
    if (m_stats.lastVacuum.isValid()) {
        int daysSince = m_stats.lastVacuum.daysTo(QDateTime::currentDateTime());
        m_lastVacuumLabel->setText(QString("Last Optimized: %1 (%2 days ago)")
            .arg(m_stats.lastVacuum.toString("yyyy-MM-dd hh:mm")).arg(daysSince));
    } else {
        m_lastVacuumLabel->setText("Last Optimized: Never");
    }
}

void DatabaseHealthDialog::runHealthCheck() {
    if (m_checkRunning || m_maintenanceRunning) {
        return;
    }
    
    m_resultsTree->clear();
    m_statusLabel->setText("Running health check...");
    m_runCheckButton->setEnabled(false);
    
    // Run health check asynchronously
    DatabaseHealthAgent::instance().runHealthCheck();
}

void DatabaseHealthDialog::performVacuum() {
    if (m_checkRunning || m_maintenanceRunning) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Optimize Database",
        "This will defragment the database and reclaim unused space.\n"
        "The operation may take a few moments.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        DatabaseHealthAgent::instance().performVacuum();
    }
}

void DatabaseHealthDialog::performReindex() {
    if (m_checkRunning || m_maintenanceRunning) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Rebuild Indexes",
        "This will rebuild all database indexes.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        DatabaseHealthAgent::instance().rebuildIndexes();
    }
}

void DatabaseHealthDialog::fixOrphanedRecords() {
    if (m_checkRunning || m_maintenanceRunning) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Fix Orphaned Records",
        "This will move orphaned assets to the root folder and clean up invalid references.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        DatabaseHealthAgent::instance().fixOrphanedRecords();
    }
}

void DatabaseHealthDialog::updateMissingFiles() {
    if (m_checkRunning || m_maintenanceRunning) {
        return;
    }
    
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Check Missing Files",
        "This will scan all assets to identify missing files.\n"
        "This may take a while for large libraries.\n\n"
        "Continue?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (reply == QMessageBox::Yes) {
        DatabaseHealthAgent::instance().updateMissingFileStatus();
    }
}

void DatabaseHealthDialog::onHealthCheckStarted() {
    m_checkRunning = true;
    m_progressBar->setVisible(true);
    m_progressLabel->setVisible(true);
    m_progressBar->setValue(0);
    m_runCheckButton->setEnabled(false);
    setActionsEnabled(false);
}

void DatabaseHealthDialog::onHealthCheckProgress(int current, int total, const QString& message) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_progressLabel->setText(message);
}

void DatabaseHealthDialog::onHealthCheckCompleted(const QVector<HealthCheckResult>& results) {
    m_checkRunning = false;
    m_lastResults = results;
    m_progressBar->setVisible(false);
    m_progressLabel->setVisible(false);
    m_runCheckButton->setEnabled(true);
    setActionsEnabled(true);
    
    displayResults(results);
    updateStatistics();
    
    // Count issues
    int warnings = 0, criticals = 0;
    for (const auto& result : results) {
        if (result.severity == HealthCheckResult::Warning) warnings++;
        if (result.severity == HealthCheckResult::Critical) criticals++;
    }
    
    if (criticals > 0) {
        m_statusLabel->setText(QString("Health check complete: %1 critical issue(s), %2 warning(s)")
            .arg(criticals).arg(warnings));
    } else if (warnings > 0) {
        m_statusLabel->setText(QString("Health check complete: %1 warning(s)").arg(warnings));
    } else {
        m_statusLabel->setText("Health check complete: Database is healthy");
    }
}

void DatabaseHealthDialog::onMaintenanceStarted(const QString& operation) {
    m_maintenanceRunning = true;
    m_statusLabel->setText(QString("Running: %1...").arg(operation));
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    setActionsEnabled(false);
    m_runCheckButton->setEnabled(false);
}

void DatabaseHealthDialog::onMaintenanceProgress(int percent) {
    m_progressBar->setValue(percent);
}

void DatabaseHealthDialog::onMaintenanceCompleted(bool success, const QString& message) {
    m_maintenanceRunning = false;
    m_progressBar->setVisible(false);
    setActionsEnabled(true);
    m_runCheckButton->setEnabled(true);

    if (success) {
        m_statusLabel->setText(message);
        QMessageBox::information(this, "Maintenance Complete", message);
        updateStatistics();
    } else {
        m_statusLabel->setText("Maintenance failed");
        QMessageBox::warning(this, "Maintenance Failed", message);
    }
}

void DatabaseHealthDialog::displayResults(const QVector<HealthCheckResult>& results) {
    m_resultsTree->clear();

    for (const auto& result : results) {
        QTreeWidgetItem* item = new QTreeWidgetItem(m_resultsTree);
        item->setIcon(0, getSeverityIcon(result.severity));
        item->setText(0, result.category);

        QString statusText;
        switch (result.severity) {
            case HealthCheckResult::Info:
                statusText = "OK";
                item->setForeground(1, QBrush(QColor("#388e3c")));
                break;
            case HealthCheckResult::Warning:
                statusText = "Warning";
                item->setForeground(1, QBrush(QColor("#f57c00")));
                break;
            case HealthCheckResult::Critical:
                statusText = "Critical";
                item->setForeground(1, QBrush(QColor("#d32f2f")));
                break;
        }
        item->setText(1, statusText);
        item->setText(2, result.message);

        // Add recommendation as child if present
        if (!result.recommendation.isEmpty()) {
            QTreeWidgetItem* recItem = new QTreeWidgetItem(item);
            recItem->setText(0, "Recommendation");
            recItem->setText(2, result.recommendation);
            recItem->setForeground(2, QBrush(QColor("#1976d2")));
            if (result.autoFixable) {
                recItem->setIcon(0, style()->standardIcon(QStyle::SP_DialogApplyButton));
            }
        }
    }

    m_resultsTree->expandAll();
}

QString DatabaseHealthDialog::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }
}

QIcon DatabaseHealthDialog::getSeverityIcon(HealthCheckResult::Severity severity) const {
    switch (severity) {
        case HealthCheckResult::Info:
            return style()->standardIcon(QStyle::SP_DialogApplyButton);
        case HealthCheckResult::Warning:
            return style()->standardIcon(QStyle::SP_MessageBoxWarning);
        case HealthCheckResult::Critical:
            return style()->standardIcon(QStyle::SP_MessageBoxCritical);
        default:
            return QIcon();
    }
}

void DatabaseHealthDialog::setActionsEnabled(bool enabled) {
    m_vacuumButton->setEnabled(enabled);
    m_reindexButton->setEnabled(enabled);
    m_fixOrphansButton->setEnabled(enabled);
    m_updateMissingButton->setEnabled(enabled);
}


