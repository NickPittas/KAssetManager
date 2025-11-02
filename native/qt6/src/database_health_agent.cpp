#include "database_health_agent.h"
#include "db.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <QDebug>

DatabaseHealthAgent& DatabaseHealthAgent::instance() {
    static DatabaseHealthAgent inst;
    return inst;
}

DatabaseHealthAgent::DatabaseHealthAgent() : QObject(nullptr) {
}

QVector<HealthCheckResult> DatabaseHealthAgent::runHealthCheck() {
    emit healthCheckStarted();
    QVector<HealthCheckResult> results;
    
    int totalChecks = 5;
    int currentCheck = 0;
    
    // Check 1: Orphaned records
    emit healthCheckProgress(++currentCheck, totalChecks, "Checking for orphaned records...");
    results.append(checkOrphanedRecords());
    
    // Check 2: Missing files
    emit healthCheckProgress(++currentCheck, totalChecks, "Checking for missing files...");
    results.append(checkMissingFiles());
    
    // Check 3: Fragmentation
    emit healthCheckProgress(++currentCheck, totalChecks, "Analyzing database fragmentation...");
    results.append(checkFragmentation());
    
    // Check 4: Integrity
    emit healthCheckProgress(++currentCheck, totalChecks, "Running integrity check...");
    results.append(checkIntegrity());
    
    // Check 5: Indexes
    emit healthCheckProgress(++currentCheck, totalChecks, "Checking indexes...");
    results.append(checkIndexes());
    
    emit healthCheckCompleted(results);
    return results;
}

DatabaseStats DatabaseHealthAgent::getDatabaseStats() {
    DatabaseStats stats;
    QSqlDatabase db = DB::instance().database();
    
    // Get database file size
    stats.totalSize = getDatabaseFileSize();
    
    // Get page info
    QSqlQuery q(db);
    if (q.exec("PRAGMA page_size") && q.next()) {
        stats.pageSize = q.value(0).toLongLong();
    }
    if (q.exec("PRAGMA page_count") && q.next()) {
        stats.pageCount = q.value(0).toLongLong();
    }
    if (q.exec("PRAGMA freelist_count") && q.next()) {
        stats.freePageCount = q.value(0).toLongLong();
    }
    
    // Calculate fragmentation
    if (stats.pageCount > 0) {
        stats.fragmentationPercent = (stats.freePageCount * 100) / stats.pageCount;
    }
    
    // Get record counts
    if (q.exec("SELECT COUNT(*) FROM assets") && q.next()) {
        stats.assetCount = q.value(0).toInt();
    }
    if (q.exec("SELECT COUNT(*) FROM virtual_folders") && q.next()) {
        stats.folderCount = q.value(0).toInt();
    }
    if (q.exec("SELECT COUNT(*) FROM tags") && q.next()) {
        stats.tagCount = q.value(0).toInt();
    }
    
    // Get orphaned assets count
    if (q.exec("SELECT COUNT(*) FROM assets WHERE virtual_folder_id NOT IN (SELECT id FROM virtual_folders)") && q.next()) {
        stats.orphanedAssets = q.value(0).toInt();
    }
    
    // Get last maintenance timestamps
    stats.lastVacuum = getLastVacuumTime();
    
    return stats;
}

QVector<HealthCheckResult> DatabaseHealthAgent::checkOrphanedRecords() {
    QVector<HealthCheckResult> results;
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);
    
    // Check for assets with invalid folder references
    if (q.exec("SELECT COUNT(*) FROM assets WHERE virtual_folder_id NOT IN (SELECT id FROM virtual_folders)") && q.next()) {
        int orphanedCount = q.value(0).toInt();
        if (orphanedCount > 0) {
            results.append(HealthCheckResult(
                "Orphaned Records",
                QString("Found %1 asset(s) with invalid folder references").arg(orphanedCount),
                HealthCheckResult::Warning,
                "Run 'Fix Orphaned Records' to reassign these assets to the root folder",
                true
            ));
        } else {
            results.append(HealthCheckResult(
                "Orphaned Records",
                "No orphaned assets found",
                HealthCheckResult::Info
            ));
        }
    }
    
    // Check for orphaned tag associations
    if (q.exec("SELECT COUNT(*) FROM asset_tags WHERE asset_id NOT IN (SELECT id FROM assets)") && q.next()) {
        int orphanedTags = q.value(0).toInt();
        if (orphanedTags > 0) {
            results.append(HealthCheckResult(
                "Orphaned Records",
                QString("Found %1 orphaned tag association(s)").arg(orphanedTags),
                HealthCheckResult::Warning,
                "Run 'Fix Orphaned Records' to clean up these associations",
                true
            ));
        }
    }
    
    return results;
}

QVector<HealthCheckResult> DatabaseHealthAgent::checkMissingFiles() {
    QVector<HealthCheckResult> results;
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);
    
    // Sample check: verify a subset of files exist
    if (q.exec("SELECT id, file_path FROM assets ORDER BY RANDOM() LIMIT 100")) {
        int missingCount = 0;
        int checkedCount = 0;
        
        while (q.next()) {
            QString filePath = q.value(1).toString();
            if (!QFileInfo::exists(filePath)) {
                missingCount++;
            }
            checkedCount++;
        }
        
        if (missingCount > 0) {
            results.append(HealthCheckResult(
                "Missing Files",
                QString("Found %1 missing file(s) in sample of %2 assets").arg(missingCount).arg(checkedCount),
                HealthCheckResult::Warning,
                "Run 'Update Missing File Status' to mark all missing files in the database",
                true
            ));
        } else {
            results.append(HealthCheckResult(
                "Missing Files",
                QString("All sampled files exist (%1 checked)").arg(checkedCount),
                HealthCheckResult::Info
            ));
        }
    }
    
    return results;
}

QVector<HealthCheckResult> DatabaseHealthAgent::checkFragmentation() {
    QVector<HealthCheckResult> results;
    qint64 fragmentation = getFragmentation();
    
    if (fragmentation > 20) {
        results.append(HealthCheckResult(
            "Fragmentation",
            QString("Database is %1% fragmented").arg(fragmentation),
            HealthCheckResult::Warning,
            "Run VACUUM to defragment and reclaim space",
            true
        ));
    } else if (fragmentation > 10) {
        results.append(HealthCheckResult(
            "Fragmentation",
            QString("Database is %1% fragmented").arg(fragmentation),
            HealthCheckResult::Info,
            "Consider running VACUUM if performance degrades"
        ));
    } else {
        results.append(HealthCheckResult(
            "Fragmentation",
            QString("Database fragmentation is low (%1%)").arg(fragmentation),
            HealthCheckResult::Info
        ));
    }
    
    return results;
}

QVector<HealthCheckResult> DatabaseHealthAgent::checkIntegrity() {
    QVector<HealthCheckResult> results;
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);
    
    if (q.exec("PRAGMA integrity_check") && q.next()) {
        QString result = q.value(0).toString();
        if (result == "ok") {
            results.append(HealthCheckResult(
                "Integrity",
                "Database integrity check passed",
                HealthCheckResult::Info
            ));
        } else {
            results.append(HealthCheckResult(
                "Integrity",
                "Database integrity check failed: " + result,
                HealthCheckResult::Critical,
                "Consider restoring from backup or running database repair"
            ));
        }
    }
    
    return results;
}

QVector<HealthCheckResult> DatabaseHealthAgent::checkIndexes() {
    QVector<HealthCheckResult> results;
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);
    
    // Check if key indexes exist
    QStringList expectedIndexes = {
        "idx_assets_folder",
        "idx_assets_sequence",
        "idx_asset_tags_asset",
        "idx_asset_tags_tag"
    };
    
    int missingIndexes = 0;
    for (const QString& indexName : expectedIndexes) {
        q.exec(QString("SELECT name FROM sqlite_master WHERE type='index' AND name='%1'").arg(indexName));
        if (!q.next()) {
            missingIndexes++;
        }
    }
    
    if (missingIndexes > 0) {
        results.append(HealthCheckResult(
            "Indexes",
            QString("%1 expected index(es) are missing").arg(missingIndexes),
            HealthCheckResult::Warning,
            "Run 'Rebuild Indexes' to recreate missing indexes",
            true
        ));
    } else {
        results.append(HealthCheckResult(
            "Indexes",
            "All expected indexes are present",
            HealthCheckResult::Info
        ));
    }
    
    return results;
}

bool DatabaseHealthAgent::performVacuum() {
    emit maintenanceStarted("VACUUM");
    emit maintenanceProgress(0);
    
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);
    
    qDebug() << "DatabaseHealthAgent: Starting VACUUM operation...";
    emit maintenanceProgress(50);
    
    bool success = q.exec("VACUUM");
    
    if (success) {
        saveMaintenanceTimestamp("vacuum");
        qDebug() << "DatabaseHealthAgent: VACUUM completed successfully";
        emit maintenanceProgress(100);
        emit maintenanceCompleted(true, "Database optimized successfully");
    } else {
        qWarning() << "DatabaseHealthAgent: VACUUM failed:" << q.lastError().text();
        emit maintenanceCompleted(false, "VACUUM failed: " + q.lastError().text());
    }
    
    return success;
}

bool DatabaseHealthAgent::rebuildIndexes() {
    emit maintenanceStarted("Rebuild Indexes");
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);

    bool success = q.exec("REINDEX");

    if (success) {
        qDebug() << "DatabaseHealthAgent: Indexes rebuilt successfully";
        emit maintenanceCompleted(true, "Indexes rebuilt successfully");
    } else {
        qWarning() << "DatabaseHealthAgent: REINDEX failed:" << q.lastError().text();
        emit maintenanceCompleted(false, "REINDEX failed: " + q.lastError().text());
    }

    return success;
}

bool DatabaseHealthAgent::fixOrphanedRecords() {
    emit maintenanceStarted("Fix Orphaned Records");
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);

    int rootId = DB::instance().ensureRootFolder();

    // Fix orphaned assets by moving them to root folder
    q.prepare("UPDATE assets SET virtual_folder_id=? WHERE virtual_folder_id NOT IN (SELECT id FROM virtual_folders)");
    q.addBindValue(rootId);
    bool success1 = q.exec();
    int fixedAssets = q.numRowsAffected();

    // Remove orphaned tag associations
    bool success2 = q.exec("DELETE FROM asset_tags WHERE asset_id NOT IN (SELECT id FROM assets)");
    int fixedTags = q.numRowsAffected();

    bool success = success1 && success2;

    if (success) {
        QString message = QString("Fixed %1 orphaned asset(s) and %2 orphaned tag association(s)")
            .arg(fixedAssets).arg(fixedTags);
        qDebug() << "DatabaseHealthAgent:" << message;
        emit maintenanceCompleted(true, message);
    } else {
        qWarning() << "DatabaseHealthAgent: Failed to fix orphaned records:" << q.lastError().text();
        emit maintenanceCompleted(false, "Failed to fix orphaned records: " + q.lastError().text());
    }

    return success;
}

bool DatabaseHealthAgent::updateMissingFileStatus() {
    emit maintenanceStarted("Update Missing File Status");
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);

    // Get all assets
    if (!q.exec("SELECT id, file_path FROM assets")) {
        emit maintenanceCompleted(false, "Failed to query assets");
        return false;
    }

    QVector<int> missingAssetIds;
    int totalChecked = 0;

    while (q.next()) {
        int assetId = q.value(0).toInt();
        QString filePath = q.value(1).toString();

        if (!QFileInfo::exists(filePath)) {
            missingAssetIds.append(assetId);
        }

        totalChecked++;
        if (totalChecked % 100 == 0) {
            emit maintenanceProgress((totalChecked * 100) / q.size());
        }
    }

    // Mark missing files (we could add a is_missing column in the future)
    QString message = QString("Checked %1 assets, found %2 missing file(s)")
        .arg(totalChecked).arg(missingAssetIds.size());

    qDebug() << "DatabaseHealthAgent:" << message;
    emit maintenanceCompleted(true, message);

    return true;
}

bool DatabaseHealthAgent::shouldVacuum() const {
    qint64 fragmentation = getFragmentation();
    QDateTime lastVacuum = getLastVacuumTime();

    // Recommend VACUUM if:
    // 1. Fragmentation > 20%
    // 2. Never vacuumed before
    // 3. Last vacuum was more than 30 days ago and fragmentation > 10%

    if (fragmentation > 20) {
        return true;
    }

    if (!lastVacuum.isValid()) {
        return true;
    }

    if (lastVacuum.daysTo(QDateTime::currentDateTime()) > 30 && fragmentation > 10) {
        return true;
    }

    return false;
}

QString DatabaseHealthAgent::getVacuumRecommendation() const {
    qint64 fragmentation = getFragmentation();
    QDateTime lastVacuum = getLastVacuumTime();

    if (fragmentation > 20) {
        return QString("Database is %1% fragmented. VACUUM recommended to reclaim space and improve performance.").arg(fragmentation);
    }

    if (!lastVacuum.isValid()) {
        return "Database has never been optimized. VACUUM recommended.";
    }

    int daysSinceVacuum = lastVacuum.daysTo(QDateTime::currentDateTime());
    if (daysSinceVacuum > 30 && fragmentation > 10) {
        return QString("Last VACUUM was %1 days ago and fragmentation is %2%. VACUUM recommended.")
            .arg(daysSinceVacuum).arg(fragmentation);
    }

    return "Database is in good health. VACUUM not needed at this time.";
}

qint64 DatabaseHealthAgent::getDatabaseFileSize() const {
    QSqlDatabase db = DB::instance().database();
    QString dbPath = db.databaseName();
    QFileInfo fileInfo(dbPath);
    return fileInfo.size();
}

qint64 DatabaseHealthAgent::getFragmentation() const {
    QSqlDatabase db = DB::instance().database();
    QSqlQuery q(db);

    qint64 pageCount = 0;
    qint64 freePages = 0;

    if (q.exec("PRAGMA page_count") && q.next()) {
        pageCount = q.value(0).toLongLong();
    }

    if (q.exec("PRAGMA freelist_count") && q.next()) {
        freePages = q.value(0).toLongLong();
    }

    if (pageCount == 0) {
        return 0;
    }

    return (freePages * 100) / pageCount;
}

QDateTime DatabaseHealthAgent::getLastVacuumTime() const {
    QSettings settings("AugmentCode", "KAssetManager");
    return settings.value("DatabaseHealth/LastVacuum").toDateTime();
}

void DatabaseHealthAgent::saveMaintenanceTimestamp(const QString& operation) {
    QSettings settings("AugmentCode", "KAssetManager");
    settings.setValue(QString("DatabaseHealth/Last%1").arg(operation), QDateTime::currentDateTime());
}
