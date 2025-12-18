#include "thumbnail_generator_dialog.h"
#include "thumbnail_cache_manager.h"
#include "db.h"
#include "project_db.h"
#include "file_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QMessageBox>
#include <QStyle>
#include <QDebug>
#include <QSqlQuery>
#include <QDir>
#include <QRegularExpression>

ThumbnailGeneratorDialog::ThumbnailGeneratorDialog(int folderId, bool recursive, QWidget* parent, bool useProjectDb)
    : QDialog(parent)
    , m_folderId(folderId)
    , m_recursive(recursive)
    , m_useProjectDb(useProjectDb)
{
    setWindowTitle("Generate Thumbnails");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setModal(false);
    resize(800, 600);
    
    buildUi();
    collectFiles();
}

ThumbnailGeneratorDialog::~ThumbnailGeneratorDialog()
{
    if (m_thread.isRunning()) {
        m_thread.quit();
        m_thread.wait();
    }
}

void ThumbnailGeneratorDialog::buildUi()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Status and file counts
    m_statusLabel = new QLabel("Ready to generate thumbnails", this);
    QFont boldFont = m_statusLabel->font();
    boldFont.setBold(true);
    m_statusLabel->setFont(boldFont);
    mainLayout->addWidget(m_statusLabel);
    
    QHBoxLayout* countsLayout = new QHBoxLayout();
    m_totalFilesLabel = new QLabel("Total Files: 0", this);
    countsLayout->addWidget(m_totalFilesLabel);
    
    m_generatedLabel = new QLabel("Generated: 0", this);
    countsLayout->addWidget(m_generatedLabel);
    countsLayout->addStretch();
    mainLayout->addLayout(countsLayout);
    
    // Progress bar
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    mainLayout->addWidget(m_progressBar);
    
    // Console output
    QLabel* outputLabel = new QLabel("Output:", this);
    mainLayout->addWidget(outputLabel);
    
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_log->setFont(monoFont);
    mainLayout->addWidget(m_log, 1);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_startBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "Start", this);
    m_startBtn->setProperty("class", "accent");
    connect(m_startBtn, &QPushButton::clicked, this, &ThumbnailGeneratorDialog::onStart);
    
    m_cancelBtn = new QPushButton("Cancel", this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setProperty("class", "danger");
    connect(m_cancelBtn, &QPushButton::clicked, this, &ThumbnailGeneratorDialog::onCancel);
    
    m_closeBtn = new QPushButton("Close", this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    btnLayout->addWidget(m_startBtn);
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(btnLayout);
}

void ThumbnailGeneratorDialog::collectFiles()
{
    m_tasks.clear();

    // Get all assets in the folder (and subfolders if recursive)
    QList<int> assetIds = m_useProjectDb
        ? ProjectDB::instance().getAssetIdsInFolder(m_folderId, m_recursive)
        : DB::instance().getAssetIdsInFolder(m_folderId, m_recursive);

    m_log->appendPlainText(QString("Scanning folder (recursive: %1)...").arg(m_recursive ? "yes" : "no"));

    // Query database for asset information
    QSqlQuery q(m_useProjectDb ? ProjectDB::instance().database() : DB::instance().database());

    // Build task list
    for (int assetId : assetIds) {
        // Query asset details including sequence information
        q.prepare("SELECT file_path, COALESCE(is_sequence, 0), sequence_pattern, sequence_start_frame, sequence_end_frame FROM assets WHERE id = ?");
        q.addBindValue(assetId);

        if (!q.exec() || !q.next()) {
            continue;
        }

        QString filePath = q.value(0).toString();
        bool isSequence = q.value(1).toBool();
        QString sequencePattern = q.value(2).toString();
        int startFrame = q.value(3).toInt();
        int endFrame = q.value(4).toInt();

        if (filePath.isEmpty()) continue;

        QFileInfo info(filePath);
        if (!info.exists()) continue;

        QString suffix = info.suffix().toLower();

        ThumbnailGeneratorWorker::Task task;
        task.filePath = filePath;

        // Determine file type
        static const QSet<QString> videoExts = {
            "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
            "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv"
        };

        static const QSet<QString> imageExts = {
            "png", "jpg", "jpeg", "bmp", "tga", "tiff", "tif",
            "gif", "webp", "exr", "dpx", "hdr", "psd"
        };

        if (isSequence) {
            // For thumbnail generation, treat each frame as an individual image
            // This ensures every file gets its own thumbnail, regardless of sequence grouping
            task.isSequence = false;  // Treat as individual image
            task.isVideo = false;

            // Try to reconstruct frame paths to generate thumbnails for all frames
            QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);

            if (!framePaths.isEmpty()) {
                // Add a task for each frame in the sequence
                for (const QString& framePath : framePaths) {
                    ThumbnailGeneratorWorker::Task frameTask;
                    frameTask.filePath = framePath;
                    frameTask.isVideo = false;
                    frameTask.isSequence = false;
                    m_tasks.append(frameTask);
                }
                continue; // Skip adding the original task
            } else {
                // Reconstruction failed - treat the file_path as a single file
                // This handles cases where the sequence metadata is incorrect
                m_log->appendPlainText(QString("WARNING: Failed to reconstruct frames for sequence: %1 - treating as single file").arg(sequencePattern));
                // Fall through to treat as individual image
            }
        }

        if (videoExts.contains(suffix)) {
            task.isVideo = true;
            task.isSequence = false;
        } else if (imageExts.contains(suffix)) {
            task.isVideo = false;
            task.isSequence = false;
        } else {
            continue; // Skip unsupported formats
        }

        m_tasks.append(task);
    }

    m_totalFiles = m_tasks.size();
    m_totalFilesLabel->setText(QString("Total Files: %1").arg(m_totalFiles));
    m_log->appendPlainText(QString("Found %1 files to process").arg(m_totalFiles));

    if (m_totalFiles == 0) {
        m_statusLabel->setText("No previewable files found in folder");
        m_startBtn->setEnabled(false);
    }
}

void ThumbnailGeneratorDialog::onStart()
{
    if (m_running) return;

    if (m_tasks.isEmpty()) {
        QMessageBox::warning(this, "No Files", "No files to process");
        return;
    }

    // Create worker if needed
    if (!m_worker) {
        m_worker = new ThumbnailGeneratorWorker();
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(this, &ThumbnailGeneratorDialog::destroyed, &m_thread, &QThread::quit);

        connect(m_worker, &ThumbnailGeneratorWorker::queueStarted, this, &ThumbnailGeneratorDialog::onQueueStarted);
        connect(m_worker, &ThumbnailGeneratorWorker::fileStarted, this, &ThumbnailGeneratorDialog::onFileStarted);
        connect(m_worker, &ThumbnailGeneratorWorker::fileProgress, this, &ThumbnailGeneratorDialog::onFileProgress);
        connect(m_worker, &ThumbnailGeneratorWorker::fileFinished, this, &ThumbnailGeneratorDialog::onFileFinished);
        connect(m_worker, &ThumbnailGeneratorWorker::queueFinished, this, &ThumbnailGeneratorDialog::onQueueFinished);
        connect(m_worker, &ThumbnailGeneratorWorker::logLine, this, &ThumbnailGeneratorDialog::onLogLine);
    }

    if (!m_thread.isRunning()) {
        m_thread.start(QThread::LowPriority);
    }

    // Get thumbnail size from settings
    ThumbnailCacheManager& cache = ThumbnailCacheManager::instance();
    QSize thumbnailSize = cache.getThumbnailSize();

    // Start generation
    QMetaObject::invokeMethod(m_worker, [this, thumbnailSize]() {
        m_worker->start(m_tasks, thumbnailSize);
    }, Qt::QueuedConnection);

    m_running = true;
    m_generatedCount = 0;
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_statusLabel->setText("Generating thumbnails...");
}

void ThumbnailGeneratorDialog::onCancel()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, &ThumbnailGeneratorWorker::cancelAll, Qt::QueuedConnection);
}

void ThumbnailGeneratorDialog::onQueueStarted(int total)
{
    m_progressBar->setValue(0);
    m_log->appendPlainText(QString("=== Starting generation of %1 files ===").arg(total));
}

void ThumbnailGeneratorDialog::onFileStarted(int index, const QString& filePath)
{
    QFileInfo info(filePath);
    m_statusLabel->setText(QString("Processing: %1").arg(info.fileName()));
}

void ThumbnailGeneratorDialog::onFileProgress(int index, int percent)
{
    // Calculate overall progress
    int overallPercent = 0;
    if (m_totalFiles > 0) {
        overallPercent = (m_generatedCount * 100 + percent) / m_totalFiles;
    }
    m_progressBar->setValue(overallPercent);
}

void ThumbnailGeneratorDialog::onFileFinished(int index, bool success, const QString& errorMsg)
{
    if (success) {
        ++m_generatedCount;
        m_generatedLabel->setText(QString("Generated: %1").arg(m_generatedCount));
    }
}

void ThumbnailGeneratorDialog::onQueueFinished(bool allSuccess)
{
    m_running = false;
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_progressBar->setValue(100);

    QString message = QString("Completed! Generated thumbnails for %1 of %2 files")
                          .arg(m_generatedCount)
                          .arg(m_totalFiles);

    m_statusLabel->setText(message);
    m_log->appendPlainText(QString("=== %1 ===").arg(message));

    // Show completion message
    QMessageBox::information(this, "Thumbnail Generation Complete", message);
}

void ThumbnailGeneratorDialog::onLogLine(const QString& line)
{
    m_log->appendPlainText(line);
}

QStringList ThumbnailGeneratorDialog::reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame)
{
    QStringList framePaths;
    QFileInfo firstFrameInfo(firstFramePath);
    QString fileName = firstFrameInfo.fileName();
    QString dirPath = firstFrameInfo.absolutePath();

    // Find the LAST frame number pattern in the first frame filename
    QRegularExpression re("(\\d{3,})");
    QRegularExpressionMatchIterator it = re.globalMatch(fileName);

    QRegularExpressionMatch lastMatch;
    bool hasMatch = false;

    while (it.hasNext()) {
        lastMatch = it.next();
        hasMatch = true;
    }

    if (!hasMatch) {
        qWarning() << "[ThumbnailGeneratorDialog] No frame number pattern found in:" << fileName;
        return framePaths;
    }

    QString frameNumberStr = lastMatch.captured(1);
    int paddingLength = frameNumberStr.length();
    int matchPos = lastMatch.capturedStart(1);

    // Extract the base name (everything before the frame number)
    QString baseName = fileName.left(matchPos);

    // Extract the suffix (everything after the frame number, including extension)
    QString suffix = fileName.mid(matchPos + paddingLength);

    // Reconstruct all frame paths
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        QString frameNum = QString("%1").arg(frame, paddingLength, 10, QChar('0'));
        QString framePath = QDir(dirPath).filePath(baseName + frameNum + suffix);

        // Only add if file exists
        if (FileUtils::fileExists(framePath)) {
            framePaths.append(framePath);
        }
    }

    return framePaths;
}
