#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include "thumbnail_generator_worker.h"

/**
 * @brief Dialog for generating persistent thumbnails with progress feedback
 * 
 * Similar to MediaConvertDialog - shows progress, file counts, and console output
 */
class ThumbnailGeneratorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThumbnailGeneratorDialog(int folderId, bool recursive, QWidget* parent = nullptr);
    ~ThumbnailGeneratorDialog();

private slots:
    void onStart();
    void onCancel();
    
    // Worker feedback
    void onQueueStarted(int total);
    void onFileStarted(int index, const QString& filePath);
    void onFileProgress(int index, int percent);
    void onFileFinished(int index, bool success, const QString& errorMsg);
    void onQueueFinished(bool allSuccess);
    void onLogLine(const QString& line);

private:
    void buildUi();
    void collectFiles();
    QStringList reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame);

    int m_folderId;
    bool m_recursive;
    
    QVector<ThumbnailGeneratorWorker::Task> m_tasks;
    
    // UI elements
    QLabel* m_statusLabel = nullptr;
    QLabel* m_totalFilesLabel = nullptr;
    QLabel* m_generatedLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
    
    // Worker thread
    QThread m_thread;
    ThumbnailGeneratorWorker* m_worker = nullptr;
    
    int m_totalFiles = 0;
    int m_generatedCount = 0;
    bool m_running = false;
};

