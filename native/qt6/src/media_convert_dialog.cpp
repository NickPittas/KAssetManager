#include "media_convert_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QApplication>
#include <QStyle>
#include <QMessageBox>

#include <algorithm>

static QString extOf(const QString& path){ return QFileInfo(path).suffix().toLower(); }

MediaConvertDialog::MediaConvertDialog(const QStringList& sourcePaths, QWidget* parent)
    : QDialog(parent), m_sources(sourcePaths)
{
    setWindowTitle("Convert to Format...");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setModal(false);
    resize(900, 600);

    buildUi();
    loadSettings();

    m_ffmpeg = locateFfmpeg();
    if (m_ffmpeg.isEmpty()) {
        m_status->setText("FFmpeg not found. Please install or set FFMPEG_ROOT.");
        m_startBtn->setEnabled(false);
    }
}

MediaConvertDialog::~MediaConvertDialog()
{
    saveSettings();
    if (m_worker) { m_worker->deleteLater(); }
    if (m_thread.isRunning()) { m_thread.quit(); m_thread.wait(); }
}

void MediaConvertDialog::buildUi()
{
    QVBoxLayout* v = new QVBoxLayout(this);

    // Sources and target dir
    QHBoxLayout* top = new QHBoxLayout();
    m_sourceList = new QListWidget(this);
    m_sourceList->setSelectionMode(QAbstractItemView::NoSelection);
    for (const QString& s : m_sources) m_sourceList->addItem(s);

    QVBoxLayout* tgt = new QVBoxLayout();
    QHBoxLayout* outRow = new QHBoxLayout();
    m_outputDir = new QLineEdit(this); m_outputDir->setPlaceholderText("Output folder...");
    m_browseBtn = new QToolButton(this); m_browseBtn->setText("...");
    connect(m_browseBtn, &QToolButton::clicked, this, &MediaConvertDialog::onBrowseOutputDir);
    outRow->addWidget(new QLabel("Target folder:"));
    outRow->addWidget(m_outputDir); outRow->addWidget(m_browseBtn);

    QHBoxLayout* formatRow = new QHBoxLayout();
    m_targetCombo = new QComboBox(this);
    formatRow->addWidget(new QLabel("Target format:")); formatRow->addWidget(m_targetCombo, 1);

    // Scaling row
    QHBoxLayout* scaleRow = new QHBoxLayout();
    m_scaleW = new QSpinBox(this); m_scaleH = new QSpinBox(this); m_lockAspect = new QCheckBox("Lock aspect", this);
    m_scaleW->setRange(0, 8192); m_scaleH->setRange(0, 8192); m_scaleW->setSpecialValueText("Auto"); m_scaleH->setSpecialValueText("Auto");
    scaleRow->addWidget(new QLabel("Width:")); scaleRow->addWidget(m_scaleW);
    scaleRow->addWidget(new QLabel("Height:")); scaleRow->addWidget(m_scaleH);
    scaleRow->addWidget(m_lockAspect);

    // Conflict policy
    QHBoxLayout* confRow = new QHBoxLayout();
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItem("Auto-rename", (int)MediaConverterWorker::ConflictAction::AutoRename);
    m_conflictCombo->addItem("Overwrite", (int)MediaConverterWorker::ConflictAction::Overwrite);
    m_conflictCombo->addItem("Skip", (int)MediaConverterWorker::ConflictAction::Skip);
    confRow->addWidget(new QLabel("If file exists:")); confRow->addWidget(m_conflictCombo);

    // Settings stack
    m_settingsStack = new QStackedWidget(this);

    // MP4 panel
    m_mp4Panel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_mp4Panel);
        m_mp4Codec = new QComboBox(m_mp4Panel); m_mp4Codec->addItems({"h264","hevc"});
        m_mp4RateMode = new QComboBox(m_mp4Panel); m_mp4RateMode->addItems({"VBR","CBR"});
        m_mp4Bitrate = new QSpinBox(m_mp4Panel); m_mp4Bitrate->setRange(1, 200); m_mp4Bitrate->setSuffix(" Mbps"); m_mp4Bitrate->setValue(8);
        h->addWidget(new QLabel("Codec:")); h->addWidget(m_mp4Codec);
        h->addWidget(new QLabel("Rate:")); h->addWidget(m_mp4RateMode);
        h->addWidget(new QLabel("Bitrate:")); h->addWidget(m_mp4Bitrate);
        h->addStretch();
    }
    // MOV panel
    m_movPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_movPanel);
        m_movCodec = new QComboBox(m_movPanel); m_movCodec->addItems({"prores_ks","h264","Animation"});
        m_movProresProf = new QComboBox(m_movPanel); m_movProresProf->addItems({"Proxy","LT","422","HQ","4444"});
        h->addWidget(new QLabel("Codec:")); h->addWidget(m_movCodec);
        h->addWidget(new QLabel("ProRes profile:")); h->addWidget(m_movProresProf);
        h->addStretch();
    }
    // JPG seq panel
    m_jpgSeqPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_jpgSeqPanel);
        m_jpgQscale = new QSpinBox(m_jpgSeqPanel); m_jpgQscale->setRange(2,31); m_jpgQscale->setValue(5);
        h->addWidget(new QLabel("Qscale (2=best,31=worst):")); h->addWidget(m_jpgQscale); h->addStretch();
    }
    // PNG seq
    m_pngSeqPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_pngSeqPanel);
        m_pngAlpha = new QCheckBox("Include alpha", m_pngSeqPanel); m_pngAlpha->setChecked(true);
        h->addWidget(m_pngAlpha); h->addStretch();
    }
    // TIF seq
    m_tifSeqPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_tifSeqPanel);
        m_tifComp = new QComboBox(m_tifSeqPanel); m_tifComp->addItems({"none","lzw","zip","packbits"});
        m_tifAlpha = new QCheckBox("Include alpha", m_tifSeqPanel); m_tifAlpha->setChecked(true);
        h->addWidget(new QLabel("Compression:")); h->addWidget(m_tifComp); h->addWidget(m_tifAlpha); h->addStretch();
    }
    // JPG
    m_jpgPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_jpgPanel);
        m_jpgQuality = new QSpinBox(m_jpgPanel); m_jpgQuality->setRange(1,100); m_jpgQuality->setValue(90);
        h->addWidget(new QLabel("Quality:")); h->addWidget(m_jpgQuality); h->addStretch();
    }
    // PNG
    m_pngPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_pngPanel);
        m_pngIncludeAlpha = new QCheckBox("Include alpha", m_pngPanel); m_pngIncludeAlpha->setChecked(true);
        h->addWidget(m_pngIncludeAlpha); h->addStretch();
    }
    // TIF
    m_tifPanel = new QWidget(this); {
        QHBoxLayout* h = new QHBoxLayout(m_tifPanel);
        m_tifCompression = new QComboBox(m_tifPanel); m_tifCompression->addItems({"none","lzw","zip","packbits"});
        m_tifIncludeAlpha = new QCheckBox("Include alpha", m_tifPanel); m_tifIncludeAlpha->setChecked(true);
        h->addWidget(new QLabel("Compression:")); h->addWidget(m_tifCompression); h->addWidget(m_tifIncludeAlpha); h->addStretch();
    }

    // Order in stack must match combo in onTargetChanged
    for (QWidget* w : {m_mp4Panel,m_movPanel,m_jpgSeqPanel,m_pngSeqPanel,m_tifSeqPanel,m_jpgPanel,m_pngPanel,m_tifPanel}) m_settingsStack->addWidget(w);

    // Decide available targets by inspecting selection
    bool hasVideo=false, hasImage=false;
    for (const QString& s : m_sources) {
        const QString e = extOf(s);
        hasVideo = hasVideo || isVideoExt(e);
        hasImage = hasImage || isImageExt(e);
    }
    if (hasVideo) {
        m_targetCombo->addItem("MP4 (H.264/H.265)", (int)MediaConverterWorker::TargetKind::VideoMP4);
        m_targetCombo->addItem("MOV (H.264/ProRes/Animation)", (int)MediaConverterWorker::TargetKind::VideoMOV);
        m_targetCombo->addItem("JPG Sequence", (int)MediaConverterWorker::TargetKind::JpgSequence);
        m_targetCombo->addItem("PNG Sequence", (int)MediaConverterWorker::TargetKind::PngSequence);
        m_targetCombo->addItem("TIF Sequence", (int)MediaConverterWorker::TargetKind::TifSequence);
    }
    if (hasImage) {
        m_targetCombo->addItem("JPG", (int)MediaConverterWorker::TargetKind::ImageJpg);
        m_targetCombo->addItem("PNG", (int)MediaConverterWorker::TargetKind::ImagePng);
        m_targetCombo->addItem("TIF", (int)MediaConverterWorker::TargetKind::ImageTif);
    }

    connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &MediaConvertDialog::onTargetChanged);

    // Right side layout
    tgt->addLayout(outRow);
    tgt->addLayout(formatRow);
    tgt->addWidget(m_settingsStack);
    tgt->addLayout(scaleRow);
    tgt->addLayout(confRow);

    top->addWidget(m_sourceList, 1);
    QWidget* rightWidget = new QWidget(this); rightWidget->setLayout(tgt);
    top->addWidget(rightWidget, 1);
    v->addLayout(top, 2);

    // Progress and log
    m_status = new QLabel("Idle", this);
    m_overallBar = new QProgressBar(this); m_overallBar->setRange(0,100);
    m_fileBar = new QProgressBar(this); m_fileBar->setRange(0,100);
    m_log = new QPlainTextEdit(this); m_log->setReadOnly(true);
    v->addWidget(m_status);
    v->addWidget(new QLabel("Overall:")); v->addWidget(m_overallBar);
    v->addWidget(new QLabel("Current file:")); v->addWidget(m_fileBar);
    v->addWidget(new QLabel("Output:")); v->addWidget(m_log, 1);

    // Buttons
    QHBoxLayout* btns = new QHBoxLayout();
    m_startBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay), "Start", this);
    m_pauseBtn = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), "Pause", this);
    m_cancelBtn = new QPushButton("Cancel", this);
    m_closeBtn = new QPushButton("Close", this);
    m_pauseBtn->setEnabled(false); m_cancelBtn->setEnabled(false);

    connect(m_startBtn, &QPushButton::clicked, this, &MediaConvertDialog::onStart);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MediaConvertDialog::onPauseResume);
    connect(m_cancelBtn, &QPushButton::clicked, this, &MediaConvertDialog::onCancel);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

    btns->addWidget(m_startBtn); btns->addWidget(m_pauseBtn); btns->addWidget(m_cancelBtn);
    btns->addStretch(); btns->addWidget(m_closeBtn);
    v->addLayout(btns);
}

void MediaConvertDialog::loadSettings()
{
    QSettings s("AugmentCode","KAssetManager");
    m_outputDir->setText(s.value("MediaConvert/OutputDir", QDir::homePath()).toString());
    m_scaleW->setValue(s.value("MediaConvert/ScaleW", 0).toInt());
    m_scaleH->setValue(s.value("MediaConvert/ScaleH", 0).toInt());
    m_lockAspect->setChecked(s.value("MediaConvert/LockAspect", true).toBool());
    m_conflictCombo->setCurrentIndex(s.value("MediaConvert/Conflict", 0).toInt());
}

void MediaConvertDialog::saveSettings()
{
    QSettings s("AugmentCode","KAssetManager");
    s.setValue("MediaConvert/OutputDir", m_outputDir->text());
    s.setValue("MediaConvert/ScaleW", m_scaleW->value());
    s.setValue("MediaConvert/ScaleH", m_scaleH->value());
    s.setValue("MediaConvert/LockAspect", m_lockAspect->isChecked());
    s.setValue("MediaConvert/Conflict", m_conflictCombo->currentIndex());
}

QString MediaConvertDialog::locateFfmpeg() const
{
    // 1) Next to app
    QString d = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    QString cand = QDir(d).filePath("ffmpeg.exe"); if (QFileInfo::exists(cand)) return cand;
#endif
    // 2) third_party
    cand = QDir(QCoreApplication::applicationDirPath()).filePath("../../third_party/ffmpeg/bin/ffmpeg.exe");
    if (QFileInfo::exists(cand)) return QFileInfo(cand).absoluteFilePath();
    // 3) FFMPEG_ROOT
    QString env = qEnvironmentVariable("FFMPEG_ROOT");
    if (!env.isEmpty()) {
        cand = QDir(env).filePath("bin/ffmpeg.exe"); if (QFileInfo::exists(cand)) return QFileInfo(cand).absoluteFilePath();
    }
    // 4) PATH
    return "ffmpeg";
}

bool MediaConvertDialog::isVideoExt(const QString& e)
{
    static const QStringList vids = {"mov","mxf","mp4","avi","mp5"};
    return vids.contains(e);
}
bool MediaConvertDialog::isImageExt(const QString& e)
{
    static const QStringList imgs = {"png","jpg","jpeg","tif","tiff","exr","iff","psd"};
    return imgs.contains(e);
}

void MediaConvertDialog::onBrowseOutputDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Choose output folder"), m_outputDir->text());
    if (!dir.isEmpty()) m_outputDir->setText(dir);
}

void MediaConvertDialog::onTargetChanged(int)
{
    // Match current combo text to stack page
    const QString txt = m_targetCombo->currentText();
    int page = 0;
    if (txt.startsWith("MP4")) page = 0;
    else if (txt.startsWith("MOV")) page = 1;
    else if (txt.startsWith("JPG Sequence")) page = 2;
    else if (txt.startsWith("PNG Sequence")) page = 3;
    else if (txt.startsWith("TIF Sequence")) page = 4;
    else if (txt == "JPG") page = 5;
    else if (txt == "PNG") page = 6;
    else if (txt == "TIF") page = 7;
    m_settingsStack->setCurrentIndex(page);
}

bool MediaConvertDialog::validateAndBuildTasks(QVector<MediaConverterWorker::Task>& outTasks, QString& error)
{
    outTasks.clear(); error.clear();
    const QString outDir = m_outputDir->text().trimmed();
    if (outDir.isEmpty()) { error = "Choose an output folder"; return false; }
    QDir d(outDir); if (!d.exists()) { if (!d.mkpath(".")) { error = "Cannot create output folder"; return false; } }

    if (m_targetCombo->count() == 0) { error = "No valid target for selection"; return false; }

    const int targetData = m_targetCombo->currentData().toInt();
    const auto target = static_cast<MediaConverterWorker::TargetKind>(targetData);

    int W = m_scaleW->value(); int H = m_scaleH->value();
    if (m_lockAspect->isChecked()) {
        if (W > 0 && H > 0) H = 0; // keep width, infer height to preserve AR
    }
    for (const QString& s : m_sources) {
        MediaConverterWorker::Task t; t.sourcePath = s; t.outputDir = outDir; t.target = target;
        t.scaleWidth = W; t.scaleHeight = H;
        t.conflict = static_cast<MediaConverterWorker::ConflictAction>(m_conflictCombo->currentData().toInt());
        if (t.conflict == MediaConverterWorker::ConflictAction::AutoRename && QFileInfo::exists(s)) {
            // handled by worker
        }
        // Fill per-target
        if (target == MediaConverterWorker::TargetKind::VideoMP4) {
            t.mp4.codec = m_mp4Codec->currentText();
            t.mp4.rateMode = (m_mp4RateMode->currentText()=="CBR")? MediaConverterWorker::RateMode::CBR: MediaConverterWorker::RateMode::VBR;
            t.mp4.bitrateMbps = m_mp4Bitrate->value();
        } else if (target == MediaConverterWorker::TargetKind::VideoMOV) {
            t.mov.codec = m_movCodec->currentText();
            t.mov.proresProfile = m_movProresProf->currentIndex();
        } else if (target == MediaConverterWorker::TargetKind::JpgSequence) {
            t.jpgSeq.qscale = m_jpgQscale->value();
        } else if (target == MediaConverterWorker::TargetKind::PngSequence) {
            t.pngSeq.includeAlpha = m_pngAlpha->isChecked();
        } else if (target == MediaConverterWorker::TargetKind::TifSequence) {
            t.tifSeq.compression = m_tifComp->currentText();
            t.tifSeq.includeAlpha = m_tifAlpha->isChecked();
        } else if (target == MediaConverterWorker::TargetKind::ImageJpg) {
            t.jpg.quality = m_jpgQuality->value();
        } else if (target == MediaConverterWorker::TargetKind::ImagePng) {
            t.png.includeAlpha = m_pngIncludeAlpha->isChecked();
        } else if (target == MediaConverterWorker::TargetKind::ImageTif) {
            t.tif.compression = m_tifCompression->currentText();
            t.tif.includeAlpha = m_tifIncludeAlpha->isChecked();
        }
        outTasks.push_back(t);
    }
    return true;
}

void MediaConvertDialog::onStart()
{
    if (m_running) return;
    QVector<MediaConverterWorker::Task> tasks; QString err;
    if (!validateAndBuildTasks(tasks, err)) { m_status->setText(err); return; }

    if (!m_worker) {
        m_worker = new MediaConverterWorker();
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(this, &MediaConvertDialog::destroyed, &m_thread, &QThread::quit);

        connect(m_worker, &MediaConverterWorker::queueStarted, this, &MediaConvertDialog::onQueueStarted);
        connect(m_worker, &MediaConverterWorker::fileStarted, this, &MediaConvertDialog::onFileStarted);
        connect(m_worker, &MediaConverterWorker::logLine, this, &MediaConvertDialog::onLogLine);
        connect(m_worker, &MediaConverterWorker::currentFileProgress, this, &MediaConvertDialog::onCurProgress);
        connect(m_worker, &MediaConverterWorker::overallProgress, this, &MediaConvertDialog::onOverall);
        connect(m_worker, &MediaConverterWorker::fileFinished, this, &MediaConvertDialog::onFileFinished);
        connect(m_worker, &MediaConverterWorker::queueFinished, this, &MediaConvertDialog::onQueueFinished);
    }

    if (!m_thread.isRunning()) m_thread.start(QThread::LowPriority);

    m_worker->setFfmpegPath(m_ffmpeg);

    // invoke start on worker's thread
    QMetaObject::invokeMethod(m_worker, [this, tasks](){ m_worker->start(tasks); }, Qt::QueuedConnection);

    m_running = true;
    m_startBtn->setEnabled(false); m_pauseBtn->setEnabled(true); m_cancelBtn->setEnabled(true);
    m_status->setText("Starting...");
}

void MediaConvertDialog::onPauseResume()
{
    if (!m_worker) return;
    if (m_pauseBtn->text() == "Pause") {
        QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::pause, Qt::QueuedConnection);
        m_pauseBtn->setText("Resume");
    } else {
        QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::resume, Qt::QueuedConnection);
        m_pauseBtn->setText("Pause");
    }
}

void MediaConvertDialog::onCancel()
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::cancelAll, Qt::QueuedConnection);
}

void MediaConvertDialog::onQueueStarted(int total)
{
    m_total = total; m_overallBar->setValue(0); m_fileBar->setValue(0);
}

void MediaConvertDialog::onFileStarted(int index, const QString& src, const QString& out, qint64 durationMs)
{
    Q_UNUSED(index);
    m_status->setText(QString("Converting: %1 -> %2").arg(QFileInfo(src).fileName(), QFileInfo(out).fileName()));
    if (durationMs <= 0) m_fileBar->setRange(0,0); else { m_fileBar->setRange(0,100); m_fileBar->setValue(0); }
}

void MediaConvertDialog::onLogLine(const QString& line)
{
    m_log->appendPlainText(line.trimmed());
}

void MediaConvertDialog::onCurProgress(int, int percent, qint64, qint64)
{
    if (m_fileBar->maximum() == 0) return; // indeterminate for images/unknown
    m_fileBar->setValue(percent);
}

void MediaConvertDialog::onOverall(int percent)
{
    m_overallBar->setValue(percent);
}

void MediaConvertDialog::onFileFinished(int, bool success, const QString& errorMsg)
{
    if (!success) {
        const QString msg = errorMsg.isEmpty() ? QStringLiteral("Conversion failed.") : errorMsg.left(500);
        m_status->setText(QString("Error: %1").arg(msg.left(200)));
        // Ask user how to proceed
        QMessageBox box(this);
        box.setWindowTitle("Conversion Error");
        box.setText(QString("%1\n\nChoose an action:").arg(msg));
        QPushButton *retryB = box.addButton("Retry", QMessageBox::AcceptRole);
        QPushButton *skipB = box.addButton("Skip", QMessageBox::DestructiveRole);
        QPushButton *cancelB = box.addButton("Cancel All", QMessageBox::RejectRole);
        box.setIcon(QMessageBox::Warning);
        box.exec();
        if (box.clickedButton() == retryB) {
            if (m_worker) QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::retryCurrent, Qt::QueuedConnection);
        } else if (box.clickedButton() == skipB) {
            if (m_worker) QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::continueAfterFailure, Qt::QueuedConnection);
        } else if (box.clickedButton() == cancelB) {
            if (m_worker) QMetaObject::invokeMethod(m_worker, &MediaConverterWorker::cancelAll, Qt::QueuedConnection);
        }
        return;
    }
}

void MediaConvertDialog::onQueueFinished(bool allSuccess)
{
    m_status->setText(allSuccess ? "All conversions completed" : "Conversion finished with errors/cancelled");
    m_running = false;
    m_startBtn->setEnabled(true); m_pauseBtn->setEnabled(false); m_cancelBtn->setEnabled(false);
}

