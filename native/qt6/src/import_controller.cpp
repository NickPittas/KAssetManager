#include "import_controller.h"

#include "db_worker.h"
#include "file_utils.h"
#include "importer.h"
#include "project_path_utils.h"
#include "sequence_detector.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <stdexcept>

namespace {

constexpr int kProgressChunk = 128;

struct ImportResult {
    int filesImported = 0;
    QList<int> changedFolderIds;
};

QVariant importResultToVariant(const ImportResult& result)
{
    QVariantMap map;
    map.insert(QStringLiteral("filesImported"), result.filesImported);
    QVariantList folders;
    folders.reserve(result.changedFolderIds.size());
    for (const int folderId : result.changedFolderIds)
        folders.append(folderId);
    map.insert(QStringLiteral("changedFolderIds"), folders);
    return map;
}

ImportResult importResultFromVariant(const QVariant& value)
{
    const QVariantMap map = value.toMap();
    ImportResult result;
    result.filesImported = map.value(QStringLiteral("filesImported")).toInt();
    const QVariantList folders = map.value(QStringLiteral("changedFolderIds")).toList();
    result.changedFolderIds.reserve(folders.size());
    for (const QVariant& folder : folders)
        result.changedFolderIds.append(folder.toInt());
    return result;
}

bool execOrThrow(QSqlQuery& query, const QString& context)
{
    if (query.exec())
        return true;
    throw std::runtime_error(QStringLiteral("%1: %2").arg(context, query.lastError().text()).toStdString());
}

int lastInsertIdOrThrow(const QSqlQuery& query, const QString& context)
{
    bool ok = false;
    const int id = query.lastInsertId().toInt(&ok);
    if (!ok || id <= 0)
        throw std::runtime_error(QStringLiteral("%1: invalid lastInsertId").arg(context).toStdString());
    return id;
}

class SqlImportDatabase final {
public:
    SqlImportDatabase(QSqlDatabase& db, ImportController::DatabaseKind kind)
        : m_db(db)
        , m_kind(kind)
    {
    }

    int ensureRootFolder()
    {
        QSqlQuery select(m_db);
        select.prepare(QStringLiteral("SELECT id FROM virtual_folders WHERE parent_id IS NULL AND name='Root' LIMIT 1"));
        execOrThrow(select, QStringLiteral("ensure root select failed"));
        if (select.next()) {
            bool ok = false;
            const int id = select.value(0).toInt(&ok);
            return ok ? id : 0;
        }

        QSqlQuery insert(m_db);
        if (m_kind == ImportController::DatabaseKind::ProjectManager)
            insert.prepare(QStringLiteral("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES('Root', NULL, NULL)"));
        else
            insert.prepare(QStringLiteral("INSERT INTO virtual_folders(name, parent_id) VALUES('Root', NULL)"));
        execOrThrow(insert, QStringLiteral("ensure root insert failed"));
        return lastInsertIdOrThrow(insert, QStringLiteral("ensure root insert failed"));
    }

    int createFolder(const QString& name, int parentId)
    {
        if (parentId <= 0)
            parentId = ensureRootFolder();

        QSqlQuery insert(m_db);
        if (m_kind == ImportController::DatabaseKind::ProjectManager) {
            int projectId = 0;
            QSqlQuery project(m_db);
            project.prepare(QStringLiteral("SELECT project_id FROM virtual_folders WHERE id=?"));
            project.addBindValue(parentId);
            execOrThrow(project, QStringLiteral("folder project lookup failed"));
            if (project.next())
                projectId = project.value(0).toInt();

            insert.prepare(QStringLiteral("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES(?, ?, ?)"));
            insert.addBindValue(name);
            insert.addBindValue(parentId);
            insert.addBindValue(projectId > 0 ? QVariant(projectId) : QVariant());
        } else {
            insert.prepare(QStringLiteral("INSERT INTO virtual_folders(name, parent_id) VALUES(?, ?)"));
            insert.addBindValue(name);
            insert.addBindValue(parentId);
        }
        execOrThrow(insert, QStringLiteral("create folder failed"));
        return lastInsertIdOrThrow(insert, QStringLiteral("create folder failed"));
    }

    int insertAssetMetadataFast(const QString& filePath, int folderId)
    {
        QFileInfo fi(filePath);
        if (!fi.exists())
            return 0;
        if (folderId <= 0)
            folderId = ensureRootFolder();

        const QString absPath = fi.absoluteFilePath();
        const QString fileType = fi.suffix().toLower();
        const QString lastModified = fi.lastModified().toString(Qt::ISODate);

        QSqlQuery select(m_db);
        select.prepare(QStringLiteral("SELECT id FROM assets WHERE file_path=?"));
        select.addBindValue(absPath);
        execOrThrow(select, QStringLiteral("asset lookup failed"));
        if (select.next()) {
            const int existingId = select.value(0).toInt();
            QSqlQuery update(m_db);
            update.prepare(QStringLiteral("UPDATE assets SET file_name=?, virtual_folder_id=?, file_size=?, file_type=?, last_modified=?, updated_at=CURRENT_TIMESTAMP WHERE id=?"));
            update.addBindValue(fi.fileName());
            update.addBindValue(folderId);
            update.addBindValue(qint64(fi.size()));
            update.addBindValue(fileType);
            update.addBindValue(lastModified);
            update.addBindValue(existingId);
            execOrThrow(update, QStringLiteral("asset update failed"));
            return existingId;
        }

        QSqlQuery insert(m_db);
        if (m_kind == ImportController::DatabaseKind::ProjectManager)
            insert.prepare(QStringLiteral("INSERT INTO assets(file_path, file_name, virtual_folder_id, file_size, file_type, last_modified, is_sequence) VALUES(?, ?, ?, ?, ?, ?, 0)"));
        else
            insert.prepare(QStringLiteral("INSERT INTO assets(file_path,file_name,virtual_folder_id,file_size,file_type,last_modified,checksum,is_sequence) VALUES(?,?,?,?,?,?,NULL,0)"));
        insert.addBindValue(absPath);
        insert.addBindValue(fi.fileName());
        insert.addBindValue(folderId);
        insert.addBindValue(qint64(fi.size()));
        insert.addBindValue(fileType);
        insert.addBindValue(lastModified);
        execOrThrow(insert, QStringLiteral("asset insert failed"));
        return lastInsertIdOrThrow(insert, QStringLiteral("asset insert failed"));
    }

    int upsertSequenceInFolderFast(const QString& sequencePattern,
                                   int startFrame,
                                   int endFrame,
                                   int frameCount,
                                   const QString& firstFramePath,
                                   int folderId,
                                   bool hasGaps,
                                   int gapCount,
                                   const QString& version)
    {
        QFileInfo fi(firstFramePath);
        if (!fi.exists())
            return 0;
        if (folderId <= 0)
            folderId = ensureRootFolder();

        QSqlQuery select(m_db);
        select.prepare(QStringLiteral("SELECT id FROM assets WHERE sequence_pattern=? AND is_sequence=1"));
        select.addBindValue(sequencePattern);
        execOrThrow(select, QStringLiteral("sequence lookup failed"));
        if (select.next()) {
            const int existingId = select.value(0).toInt();
            QSqlQuery update(m_db);
            update.prepare(QStringLiteral("UPDATE assets SET file_path=?, file_name=?, virtual_folder_id=?, file_size=?, sequence_start_frame=?, sequence_end_frame=?, sequence_frame_count=?, sequence_has_gaps=?, sequence_gap_count=?, sequence_version=?, updated_at=CURRENT_TIMESTAMP WHERE id=?"));
            update.addBindValue(fi.absoluteFilePath());
            update.addBindValue(sequencePattern);
            update.addBindValue(folderId);
            update.addBindValue(qint64(fi.size()));
            update.addBindValue(startFrame);
            update.addBindValue(endFrame);
            update.addBindValue(frameCount);
            update.addBindValue(hasGaps ? 1 : 0);
            update.addBindValue(gapCount);
            update.addBindValue(version);
            update.addBindValue(existingId);
            execOrThrow(update, QStringLiteral("sequence update failed"));
            return existingId;
        }

        QSqlQuery insert(m_db);
        insert.prepare(QStringLiteral("INSERT INTO assets(file_path,file_name,virtual_folder_id,file_size,is_sequence,sequence_pattern,sequence_start_frame,sequence_end_frame,sequence_frame_count,sequence_has_gaps,sequence_gap_count,sequence_version) VALUES(?,?,?,?,1,?,?,?,?,?,?,?)"));
        insert.addBindValue(fi.absoluteFilePath());
        insert.addBindValue(sequencePattern);
        insert.addBindValue(folderId);
        insert.addBindValue(qint64(fi.size()));
        insert.addBindValue(sequencePattern);
        insert.addBindValue(startFrame);
        insert.addBindValue(endFrame);
        insert.addBindValue(frameCount);
        insert.addBindValue(hasGaps ? 1 : 0);
        insert.addBindValue(gapCount);
        insert.addBindValue(version);
        execOrThrow(insert, QStringLiteral("sequence insert failed"));
        return lastInsertIdOrThrow(insert, QStringLiteral("sequence insert failed"));
    }

private:
    QSqlDatabase& m_db;
    ImportController::DatabaseKind m_kind;
};

using ProgressCallback = std::function<void(int, int, QString, QString)>;

class ImportJob final : public IDbJob {
public:
    ImportJob(ImportController::DatabaseKind databaseKind,
              QStringList filePaths,
              QStringList folderPaths,
              int targetFolderId,
              bool importFolderContents,
              bool sequenceDetectionEnabled,
              ProgressCallback progressCallback)
        : m_databaseKind(databaseKind)
        , m_filePaths(std::move(filePaths))
        , m_folderPaths(std::move(folderPaths))
        , m_targetFolderId(targetFolderId)
        , m_importFolderContents(importFolderContents)
        , m_sequenceDetectionEnabled(sequenceDetectionEnabled)
        , m_progressCallback(std::move(progressCallback))
    {
    }

    QVariant run(QSqlDatabase& db) override
    {
        SqlImportDatabase importDb(db, m_databaseKind);
        ImportResult result;
        if (!db.transaction())
            throw std::runtime_error(db.lastError().text().toStdString());

        bool committed = false;
        try {
            for (const QString& folderPath : std::as_const(m_folderPaths)) {
                if (m_importFolderContents)
                    importFolderContents(importDb, folderPath, m_targetFolderId, result);
                else
                    importFolder(importDb, folderPath, m_targetFolderId, result);
            }
            importFiles(importDb, m_filePaths, m_targetFolderId, result);
            if (!db.commit())
                throw std::runtime_error(db.lastError().text().toStdString());
            committed = true;
        } catch (...) {
            if (!committed)
                db.rollback();
            throw;
        }
        return importResultToVariant(result);
    }

private:
    static QString normalizedPath(const QString& path)
    {
        return QFileInfo(path).absoluteFilePath();
    }

    void reportProgress(int current, int total, const QString& fileName = QString(), const QString& folderName = QString()) const
    {
        if (m_progressCallback)
            m_progressCallback(current, total, fileName, folderName);
    }

    void addChangedFolder(ImportResult& result, int folderId) const
    {
        if (folderId > 0 && !result.changedFolderIds.contains(folderId))
            result.changedFolderIds.append(folderId);
    }

    void importFiles(SqlImportDatabase& db, const QStringList& filePaths, int folderId, ImportResult& result) const
    {
        const int total = filePaths.size();
        for (int i = 0; i < total; ++i) {
            const QString& filePath = filePaths.at(i);
            if (Importer::isMediaFile(filePath) && db.insertAssetMetadataFast(filePath, folderId) > 0) {
                ++result.filesImported;
                addChangedFolder(result, folderId);
            }
            if ((i % kProgressChunk) == 0 || i + 1 == total)
                reportProgress(i + 1, total, QFileInfo(filePath).fileName());
        }
    }

    void buildFolders(SqlImportDatabase& db,
                      const QString& dirPath,
                      int rootFolderId,
                      bool normalizeKeys,
                      QHash<QString, int>& folderIds) const
    {
        auto keyForPath = [normalizeKeys](const QString& path) {
            return normalizeKeys ? ProjectPathUtils::keyForPath(path) : path;
        };

        folderIds.insert(keyForPath(dirPath), rootFolderId);
        QList<QString> pending;
        pending.push_back(dirPath);
        while (!pending.isEmpty()) {
            const QString cur = pending.front();
            pending.pop_front();
            const int curId = folderIds.value(keyForPath(cur), rootFolderId);
            const QDir d(cur);
            const QStringList folders = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QString& folder : folders) {
                const QString sub = QDir(cur).filePath(folder);
                const int id = db.createFolder(folder, curId);
                folderIds.insert(keyForPath(sub), id);
                pending.push_back(sub);
            }
        }
    }

    void collectFilesByDir(const QString& dirPath,
                           bool mediaOnly,
                           bool normalizeKeys,
                           QHash<QString, QStringList>& filesByDir,
                           int& totalFiles) const
    {
        auto keyForPath = [normalizeKeys](const QString& path) {
            return normalizeKeys ? ProjectPathUtils::keyForPath(path) : path;
        };

        QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString fp = it.next();
            if (mediaOnly && !Importer::isMediaFile(fp))
                continue;
            ++totalFiles;
            filesByDir[keyForPath(QFileInfo(fp).absolutePath())].append(fp);
        }
    }

    void importFolder(SqlImportDatabase& db, const QString& dirPath, int parentFolderId, ImportResult& result) const
    {
        QDir dir(dirPath);
        if (!dir.exists())
            return;
        if (parentFolderId <= 0)
            parentFolderId = db.ensureRootFolder();

        QString topName = QFileInfo(dirPath).fileName();
        if (topName.isEmpty())
            topName = dir.dirName();
        const int topId = db.createFolder(topName, parentFolderId);
        if (topId <= 0)
            return;
        reportProgress(0, 0, QString(), topName);

        QHash<QString, int> folderIds;
        buildFolders(db, dirPath, topId, false, folderIds);

        int totalFiles = 0;
        QHash<QString, QStringList> filesByDir;
        collectFilesByDir(dirPath, true, false, filesByDir, totalFiles);
        importGroupedFiles(db, filesByDir, folderIds, topId, totalFiles, result);
    }

    void importFolderContents(SqlImportDatabase& db, const QString& dirPath, int targetFolderId, ImportResult& result) const
    {
        QDir dir(dirPath);
        if (!dir.exists())
            return;
        if (targetFolderId <= 0)
            targetFolderId = db.ensureRootFolder();
        reportProgress(0, 0, QString(), QFileInfo(dirPath).fileName());

        QHash<QString, int> folderIds;
        buildFolders(db, dirPath, targetFolderId, true, folderIds);

        int totalFiles = 0;
        QHash<QString, QStringList> filesByDir;
        collectFilesByDir(dirPath, false, true, filesByDir, totalFiles);
        importGroupedFiles(db, filesByDir, folderIds, targetFolderId, totalFiles, result);
    }

    void importGroupedFiles(SqlImportDatabase& db,
                            const QHash<QString, QStringList>& filesByDir,
                            const QHash<QString, int>& folderIds,
                            int fallbackFolderId,
                            int totalFiles,
                            ImportResult& result) const
    {
        int currentFile = 0;
        reportProgress(0, totalFiles);

        for (auto dirIt = filesByDir.cbegin(); dirIt != filesByDir.cend(); ++dirIt) {
            const int folderId = folderIds.value(dirIt.key(), fallbackFolderId);
            const QStringList files = dirIt.value();
            QSet<QString> sequenceFiles;

            if (m_sequenceDetectionEnabled) {
                const QVector<ImageSequence> sequences = SequenceDetector::detectSequences(files);
                for (const ImageSequence& sequence : sequences) {
                    if (db.upsertSequenceInFolderFast(sequence.pattern,
                                                      sequence.startFrame,
                                                      sequence.endFrame,
                                                      sequence.frameCount,
                                                      sequence.firstFramePath,
                                                      folderId,
                                                      sequence.hasGaps,
                                                      sequence.gapCount,
                                                      sequence.version) > 0) {
                        ++result.filesImported;
                        addChangedFolder(result, folderId);
                    }
                    for (const QString& framePath : sequence.framePaths) {
                        sequenceFiles.insert(framePath);
                        ++currentFile;
                    }
                    reportProgress(currentFile, totalFiles, sequence.pattern);
                }
            }

            for (const QString& filePath : files) {
                if (sequenceFiles.contains(filePath))
                    continue;
                if (db.insertAssetMetadataFast(filePath, folderId) > 0) {
                    ++result.filesImported;
                    addChangedFolder(result, folderId);
                }
                ++currentFile;
                if ((currentFile % kProgressChunk) == 0 || currentFile == totalFiles)
                    reportProgress(currentFile, totalFiles, QFileInfo(filePath).fileName());
            }
        }
        reportProgress(totalFiles, totalFiles);
    }

    ImportController::DatabaseKind m_databaseKind;
    QStringList m_filePaths;
    QStringList m_folderPaths;
    int m_targetFolderId = 0;
    bool m_importFolderContents = false;
    bool m_sequenceDetectionEnabled = true;
    ProgressCallback m_progressCallback;
};

} // namespace

ImportController::ImportController(QObject* parent)
    : QObject(parent)
    , m_worker(new DbWorker(this))
{
    connect(m_worker, &DbWorker::jobFinished, this, &ImportController::onJobFinished);
    connect(m_worker, &DbWorker::jobFailed, this, &ImportController::onJobFailed);
}

ImportController::~ImportController()
{
    stop();
}

bool ImportController::start(DatabaseKind databaseKind, const QString& dbFilePath)
{
    if (m_worker->isRunning())
        return false;
    m_databaseKind = databaseKind;
    const QString connectionName = databaseKind == DatabaseKind::ProjectManager
        ? QStringLiteral("project_import_worker")
        : QStringLiteral("asset_import_worker");
    return m_worker->start(connectionName, dbFilePath);
}

void ImportController::stop()
{
    m_activeJobId = 0;
    if (m_worker)
        m_worker->stop();
}

bool ImportController::isRunning() const
{
    return m_worker && m_worker->isRunning();
}

bool ImportController::importToAssetLibrary(const QStringList& filePaths,
                                            const QStringList& folderPaths,
                                            int targetFolderId,
                                            bool sequenceDetectionEnabled)
{
    return submitImport(filePaths, folderPaths, targetFolderId, false, sequenceDetectionEnabled);
}

bool ImportController::importFolderContentsToProject(const QString& dirPath,
                                                     int targetFolderId,
                                                     bool sequenceDetectionEnabled)
{
    return submitImport(QStringList(), QStringList{dirPath}, targetFolderId, true, sequenceDetectionEnabled);
}

bool ImportController::submitImport(const QStringList& filePaths,
                                    const QStringList& folderPaths,
                                    int targetFolderId,
                                    bool importFolderContents,
                                    bool sequenceDetectionEnabled)
{
    if (!m_worker || !m_worker->isRunning() || m_activeJobId != 0)
        return false;
    if (filePaths.isEmpty() && folderPaths.isEmpty())
        return false;

    const QPointer<ImportController> self(this);
    auto callback = [self](int current, int total, const QString& fileName, const QString& folderName) {
        if (!self)
            return;
        QMetaObject::invokeMethod(self, [self, current, total, fileName, folderName] {
            if (!self)
                return;
            if (!folderName.isEmpty())
                emit self->currentFolderChanged(folderName);
            if (!fileName.isEmpty())
                emit self->currentFileChanged(fileName);
            emit self->progressChanged(current, total);
        }, Qt::QueuedConnection);
    };

    auto job = std::make_shared<ImportJob>(m_databaseKind,
                                           filePaths,
                                           folderPaths,
                                           targetFolderId,
                                           importFolderContents,
                                           sequenceDetectionEnabled,
                                           std::move(callback));
    m_activeJobId = m_worker->submit(job);
    return m_activeJobId != 0;
}

void ImportController::onJobFinished(int jobId, const QVariant& result)
{
    if (jobId != m_activeJobId)
        return;
    m_activeJobId = 0;
    const ImportResult importResult = importResultFromVariant(result);
    emit importFinished(importResult.filesImported, importResult.changedFolderIds);
}

void ImportController::onJobFailed(int jobId, const QString& error)
{
    if (jobId != m_activeJobId)
        return;
    m_activeJobId = 0;
    emit importFailed(error);
}
