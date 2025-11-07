#pragma once

#include <QDialog>
#include <QListWidget>
#include <QComboBox>
#include <QStackedWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include <QSettings>

#include "media_converter_worker.h"

class MediaConvertDialog : public QDialog {
    Q_OBJECT
public:
    explicit MediaConvertDialog(const QStringList& sourcePaths, QWidget* parent=nullptr);
    ~MediaConvertDialog();

private slots:
    void onBrowseOutputDir();
    void onTargetChanged(int idx);
    void onStart();
    void onCancel();
    void onVerifySequence();

    // Worker feedback
    void onQueueStarted(int total);
    void onFileStarted(int index, const QString& src, const QString& out, qint64 durationMs);
    void onLogLine(const QString& line);
    void onCurProgress(int index, int percent, qint64 outMs, qint64 totalMs);
    void onOverall(int percent);
    void onFileFinished(int index, bool success, const QString& errorMsg);
    void onQueueFinished(bool allSuccess);

private:
    void buildUi();
    void loadSettings();
    void saveSettings();
    QString locateFfmpeg() const;
    QString locateMagick() const;

    bool validateAndBuildTasks(QVector<MediaConverterWorker::Task>& outTasks, QString& error);
    static bool isVideoExt(const QString& ext);
    static bool isImageExt(const QString& ext);

private:
    // Inputs
    QStringList m_sources;

    // UI
    QListWidget* m_sourceList = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QToolButton* m_browseBtn = nullptr;
    QComboBox* m_targetCombo = nullptr;
    QToolButton* m_verifyBtn = nullptr;
    QStackedWidget* m_settingsStack = nullptr;

    // MP4 panel
    QWidget* m_mp4Panel = nullptr; QComboBox* m_mp4Codec = nullptr; QComboBox* m_mp4RateMode = nullptr; QSpinBox* m_mp4Bitrate = nullptr; QSpinBox* m_mp4Fps = nullptr;
    // MOV panel
    QWidget* m_movPanel = nullptr; QComboBox* m_movCodec = nullptr; QComboBox* m_movProresProf = nullptr; QSpinBox* m_movFps = nullptr;
    // JPG seq
    QWidget* m_jpgSeqPanel = nullptr; QSpinBox* m_jpgQscale = nullptr; QSpinBox* m_jpgSeqPadDigits = nullptr; QSpinBox* m_jpgSeqStart = nullptr;
    // PNG seq
    QWidget* m_pngSeqPanel = nullptr; QCheckBox* m_pngAlpha = nullptr; QSpinBox* m_pngSeqPadDigits = nullptr; QSpinBox* m_pngSeqStart = nullptr;
    // TIF seq
    QWidget* m_tifSeqPanel = nullptr; QComboBox* m_tifComp = nullptr; QCheckBox* m_tifAlpha = nullptr; QSpinBox* m_tifSeqPadDigits = nullptr; QSpinBox* m_tifSeqStart = nullptr;
    // JPG
    QWidget* m_jpgPanel = nullptr; QSpinBox* m_jpgQuality = nullptr;
    // PNG
    QWidget* m_pngPanel = nullptr; QCheckBox* m_pngIncludeAlpha = nullptr;
    // TIF
    QWidget* m_tifPanel = nullptr; QComboBox* m_tifCompression = nullptr; QCheckBox* m_tifIncludeAlpha = nullptr;

    // Scaling
    QSpinBox* m_scaleW = nullptr; QSpinBox* m_scaleH = nullptr; QCheckBox* m_lockAspect = nullptr;

    // Conflict policy
    QComboBox* m_conflictCombo = nullptr;

    // Progress
    QProgressBar* m_overallBar = nullptr; QProgressBar* m_fileBar = nullptr; QLabel* m_status = nullptr; QPlainTextEdit* m_log = nullptr;

    // Buttons
    QPushButton* m_startBtn = nullptr; QPushButton* m_cancelBtn = nullptr; QPushButton* m_closeBtn = nullptr;

    // Worker
    QThread m_thread; MediaConverterWorker* m_worker = nullptr;
    QString m_ffmpeg;
    QString m_magick;
    int m_total = 0;
    bool m_running = false;
};

