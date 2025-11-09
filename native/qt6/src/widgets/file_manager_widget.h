#pragma once

#include <QWidget>
#include <QAbstractItemView>
#include <QAudioOutput>
#include <QCheckBox>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPdfDocument>
#include <QPdfView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QModelIndex>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QMediaPlayer>
#include <QSettings>


#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QUrl>
#include <QDesktopServices>
#include <QVideoWidget>
#include "ui/file_type_helpers.h"
#include "ui/icon_helpers.h"
#include <QBoxLayout>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QDesktopServices>
#include <QUrl>
#include "mainwindow.h"
#include "widgets/sequence_grouping_proxy_model.h"
#include "widgets/fm_drag_views.h"
#include "preview_overlay.h"
#include "log_manager.h"


#include <QHash>
#include <QPersistentModelIndex>
#include <QSet>

class QListWidgetItem;
class QFileSystemModel;
class QFileSystemWatcher;
class QListWidget;
class QTreeView;
class QToolButton;
class QSlider;
class QStackedWidget;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QVideoWidget;
class QPlainTextEdit;
class QTableView;
class QStandardItemModel;
class QPdfDocument;
class QPdfView;
class QGraphicsItem;
class QCheckBox;
class QMediaPlayer;
class QAudioOutput;
class QPushButton;
class QLabel;
class QShortcut;
class FileOpsProgressDialog;
class SequenceGroupingProxyModel;
class FmGridViewEx;
class FmListViewEx;
class PreviewOverlay;
class ImportProgressDialog;
class MainWindow;

class FileManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FileManagerWidget(MainWindow* host, QWidget* parent = nullptr);
    ~FileManagerWidget();
    void ensurePreviewInfoLayout();


signals:
    void navigateToPathRequested(const QString& path, bool addToHistory);

private:
    void setupUi();
    void bindHostPointers();
    void navigateToPath(const QString& path, bool addToHistory);
    void scrollTreeToPath(const QString& path);
    void updateNavigationButtons();

    MainWindow* m_host = nullptr;

public:
    QSplitter *fmSplitter = nullptr;
    SequenceGroupingProxyModel *fmProxyModel = nullptr;
    QToolButton *fmGroupSequencesCheckBox = nullptr;
    bool fmGroupSequences = true;
    QToolButton *fmHideFoldersCheckBox = nullptr;
    bool fmHideFolders = false;

    QSplitter *fmLeftSplitter = nullptr;
    QSplitter *fmRightSplitter = nullptr;
    QSplitter *fmPreviewInfoSplitter = nullptr;
    QListWidget *fmFavoritesList = nullptr;
    QTreeView *fmTree = nullptr;
    QFileSystemModel *fmTreeModel = nullptr;
    QFileSystemModel *fmDirModel = nullptr;
    QWidget *fmToolbar = nullptr;
    QToolButton *fmBackButton = nullptr;
    QToolButton *fmUpButton = nullptr;
    QToolButton *fmViewModeButton = nullptr;
    QSlider *fmThumbnailSizeSlider = nullptr;
    QToolButton *fmPreviewToggleButton = nullptr;
    QStackedWidget *fmViewStack = nullptr;
    FmGridViewEx *fmGridView = nullptr;
    FmListViewEx *fmListView = nullptr;
    bool fmIsGridMode = true;

    QFileSystemWatcher *fmDirectoryWatcher = nullptr;

    QStringList fmNavigationHistory;
    int fmNavigationIndex = -1;

    QStringList fmFavorites;

    QWidget *fmPreviewPanel = nullptr;
    QGraphicsView *fmImageView = nullptr;
    QGraphicsScene *fmImageScene = nullptr;
    QGraphicsPixmapItem *fmImageItem = nullptr;
    QVideoWidget *fmVideoWidget = nullptr;
    QPlainTextEdit *fmTextView = nullptr;
    QTableView *fmCsvView = nullptr;
    QStandardItemModel *fmCsvModel = nullptr;
    QPdfDocument *fmPdfDoc = nullptr;
    QPdfView *fmPdfView = nullptr;
    int fmPdfCurrentPage = 0;
    QToolButton *fmPdfPrevBtn = nullptr;
    QToolButton *fmPdfNextBtn = nullptr;
    QLabel *fmPdfPageLabel = nullptr;
    QGraphicsView *fmSvgView = nullptr;
    QGraphicsScene *fmSvgScene = nullptr;
    QGraphicsItem *fmSvgItem = nullptr;
    QCheckBox *fmAlphaCheck = nullptr;
    bool fmImageFitToView = true;
    QImage fmOriginalImage;
    QString fmCurrentPreviewPath;
    bool fmPreviewHasAlpha = false;
    bool fmAlphaOnlyMode = false;
    QPoint fmPreviewDragStartPos;
    bool fmPreviewDragPending = false;
    QMediaPlayer *fmMediaPlayer = nullptr;
    QAudioOutput *fmAudioOutput = nullptr;
    QPushButton *fmPlayPauseBtn = nullptr;
    QPushButton *fmPrevFrameBtn = nullptr;
    QPushButton *fmNextFrameBtn = nullptr;
    QHash<QString, QShortcut*> fmShortcutObjs;

    QSlider *fmPositionSlider = nullptr;
    QLabel *fmTimeLabel = nullptr;
    QSlider *fmVolumeSlider = nullptr;

    bool fmIsSequence = false;
    QStringList fmSequenceFramePaths;
    int fmSequenceStartFrame = 0;
    int fmSequenceEndFrame = 0;
    int fmSequenceCurrentIndex = 0;
    QTimer *fmSequenceTimer = nullptr;
    bool fmSequencePlaying = false;
    double fmSequenceFps = 24.0;
    bool fmWasPlayingBeforeSeek = false;

    QPushButton *fmMuteBtn = nullptr;

    QWidget *fmInfoPanel = nullptr;
    QLabel *fmInfoFileName = nullptr;
    QLabel *fmInfoFilePath = nullptr;
    QLabel *fmInfoFileSize = nullptr;
    QLabel *fmInfoFileType = nullptr;
    QLabel *fmInfoDimensions = nullptr;
    QLabel *fmInfoCreated = nullptr;
    QLabel *fmInfoModified = nullptr;
    QLabel *fmInfoPermissions = nullptr;

    QStringList fmClipboard;
    bool fmClipboardCutMode = false;
    FileOpsProgressDialog *fileOpsDialog = nullptr;

    QPersistentModelIndex fmOverlayCurrentIndex;
    QPointer<QAbstractItemView> fmOverlaySourceView;

private slots:
    void onFmTreeActivated(const QModelIndex &index);
    void onFmViewModeToggled();
    void onFmThumbnailSizeChanged(int size);
    void onFmGroupSequencesToggled(bool checked);
    void onFmHideFoldersToggled(bool checked);
    void onFmRemoveFavorite();
    void onFmFavoriteActivated(QListWidgetItem* item);

    void onFmTreeContextMenu(const QPoint &pos);
    void onFmShowContextMenu(const QPoint &pos);

public slots:
    void onFmAddToFavorites();
    void onFmItemDoubleClicked(const QModelIndex &index);

    void onFmRefresh();
    void onFmNewFolder();
    void onFmRename();
    void onFmCopy();
    void onFmCut();
    void onFmPaste();
    void onFmDelete();
    void onFmDeletePermanent();
    void onFmCreateFolderWithSelected();
    void onFmBulkRename();
    void onFmNavigateBack();
    void onFmNavigateUp();
};




inline void FileManagerWidget::onFmItemDoubleClicked(const QModelIndex &index)
{
    QModelIndex idx = index.sibling(index.row(), 0);
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);

    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;

    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = m_host->reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (!m_host->previewOverlay) {
                m_host->previewOverlay = new PreviewOverlay(m_host);
                m_host->previewOverlay->setGeometry(m_host->rect());
                QObject::connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
                QObject::connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, m_host, &MainWindow::changePreview);
            } else {
                m_host->previewOverlay->stopPlayback();
            }
            QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus())
                ? static_cast<QAbstractItemView*>(fmGridView)
                : static_cast<QAbstractItemView*>(fmListView);
            m_host->fmOverlayCurrentIndex = QPersistentModelIndex(idx);
            m_host->fmOverlaySourceView = srcView;
            int pad = 0;
            const QString fileName = QFileInfo(info.reprPath).fileName();
            for (int i = fileName.size() - 1; i >= 0; --i) {
                if (fileName.at(i).isDigit()) {
                    int j = i;
                    while (j >= 0 && fileName.at(j).isDigit()) --j;
                    pad = i - j;
                    break;
                }
            }
            if (pad == 0) pad = QString::number(info.start).length();
            const QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            const QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            const QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            m_host->previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    const QFileInfo fi(path);
    if (fi.isDir()) {
        emit navigateToPathRequested(path, true);
        return;
    }

    const QString ext = fi.suffix();
    if (isImageFile(ext) || isVideoFile(ext)) {
        if (!m_host->previewOverlay) {
            m_host->previewOverlay = new PreviewOverlay(m_host);
            m_host->previewOverlay->setGeometry(m_host->rect());
            QObject::connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
            QObject::connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, m_host, &MainWindow::changePreview);
        } else {
            m_host->previewOverlay->stopPlayback();
        }
        QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus())
            ? static_cast<QAbstractItemView*>(fmGridView)
            : static_cast<QAbstractItemView*>(fmListView);
        m_host->fmOverlayCurrentIndex = QPersistentModelIndex(idx);
        m_host->fmOverlaySourceView = srcView;
        m_host->previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}


inline void FileManagerWidget::ensurePreviewInfoLayout()
{
    LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout enter", "DEBUG");
    if (!fmToolbar) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no fmToolbar", "DEBUG"); return; }
    QWidget *right = fmToolbar->parentWidget();
    if (!right) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no right", "DEBUG"); return; }
    auto rightLayout = qobject_cast<QBoxLayout*>(right->layout());
    if (!rightLayout) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no layout", "DEBUG"); return; }

    // If duplicate view stacks exist (bug from earlier setup), adopt the one that actually has views
    if (fmViewStack) {
        auto stacks = right->findChildren<QStackedWidget*>(QString(), Qt::FindDirectChildrenOnly);
        QStackedWidget *realStack = fmViewStack;
        for (auto s : stacks) { if (s && s->count() >= 2) { realStack = s; break; } }
        if (realStack != fmViewStack && realStack) {
            int i = rightLayout->indexOf(fmViewStack);
            if (i >= 0) { auto it = rightLayout->takeAt(i); Q_UNUSED(it); fmViewStack->deleteLater(); }
            fmViewStack = realStack;
        }
        // Drop any stray stacked widgets except fmViewStack
        for (int i = rightLayout->count() - 1; i >= 0; --i) {
            QWidget *w = rightLayout->itemAt(i) ? rightLayout->itemAt(i)->widget() : nullptr;
            if (w && w != fmViewStack && qobject_cast<QStackedWidget*>(w)) {
                rightLayout->takeAt(i);
                w->deleteLater();
            }
        }
    }

    // If we've already built the right splitter, ensure it's in the layout and exit
    if (fmRightSplitter && fmPreviewInfoSplitter && fmPreviewPanel && fmInfoPanel) {
        if (rightLayout->indexOf(fmRightSplitter) < 0) rightLayout->addWidget(fmRightSplitter);
        bindHostPointers();
        return;
    }

    // Build right side splitter: [viewContainer(fmViewStack)] | [Preview/Info splitter]
    fmRightSplitter = new QSplitter(Qt::Horizontal, right);

    QWidget *viewContainer = new QWidget(fmRightSplitter);
    auto vc = new QVBoxLayout(viewContainer);
    vc->setContentsMargins(0,0,0,0);
    vc->setSpacing(0);
    if (fmViewStack) {
        fmViewStack->setParent(viewContainer);
        vc->addWidget(fmViewStack);
    }

    // Preview panel (compact but complete)
    fmPreviewPanel = new QWidget(fmRightSplitter);
    fmPreviewPanel->setMinimumWidth(260);
    auto pv = new QVBoxLayout(fmPreviewPanel);
    pv->setContentsMargins(0,0,0,0);
    pv->setSpacing(6);

    // Image preview
    fmImageScene = new QGraphicsScene(fmPreviewPanel);
    fmImageItem = new QGraphicsPixmapItem();
    fmImageScene->addItem(fmImageItem);
    fmImageView = new QGraphicsView(fmImageScene, fmPreviewPanel);
    pv->addWidget(fmImageView, 1);

    // Video preview
    fmVideoWidget = new QVideoWidget(fmPreviewPanel);
    fmMediaPlayer = new QMediaPlayer(fmPreviewPanel);
    fmAudioOutput = new QAudioOutput(fmPreviewPanel);
    fmMediaPlayer->setVideoOutput(fmVideoWidget);
    fmMediaPlayer->setAudioOutput(fmAudioOutput);
    // Log media errors so we can diagnose unsupported MP4s in the pane
    QObject::connect(fmMediaPlayer, &QMediaPlayer::errorOccurred, this,
                     [this](QMediaPlayer::Error error, const QString &errorString){
                         Q_UNUSED(error);
                         LogManager::instance().addLog(QString("[FM Preview] QMediaPlayer error: %1").arg(errorString), "ERROR");
                         if (fmTimeLabel) fmTimeLabel->setText(QString("Error: %1").arg(errorString));
                     });
    fmVideoWidget->hide();
    pv->addWidget(fmVideoWidget, 1);

    // Text/CSV/PDF/SVG
    fmTextView = new QPlainTextEdit(fmPreviewPanel); fmTextView->setReadOnly(true); fmTextView->hide(); pv->addWidget(fmTextView, 1);
    fmCsvModel = new QStandardItemModel(this); fmCsvView = new QTableView(fmPreviewPanel); fmCsvView->setModel(fmCsvModel); fmCsvView->hide(); pv->addWidget(fmCsvView, 1);
    fmPdfDoc = new QPdfDocument(fmPreviewPanel); fmPdfView = new QPdfView(fmPreviewPanel); fmPdfView->setDocument(fmPdfDoc); fmPdfView->hide(); pv->addWidget(fmPdfView, 1);
    fmSvgScene = new QGraphicsScene(fmPreviewPanel); fmSvgView = new QGraphicsView(fmSvgScene, fmPreviewPanel); fmSvgView->hide(); pv->addWidget(fmSvgView, 1);

    // Media controls (Prev | Play/Pause | Next | Position | Time | Mute | Volume)
    QWidget *ctrl = new QWidget(fmPreviewPanel);
    auto cr = new QHBoxLayout(ctrl); cr->setContentsMargins(6,0,6,6); cr->setSpacing(6);

    fmPrevFrameBtn = new QPushButton(ctrl);
    fmPrevFrameBtn->setIcon(icoMediaPrevFrame());

    fmPlayPauseBtn = new QPushButton(ctrl);
    fmPlayPauseBtn->setIcon(icoMediaPlay());

    fmNextFrameBtn = new QPushButton(ctrl);
    fmNextFrameBtn->setIcon(icoMediaNextFrame());

    fmPositionSlider = new QSlider(Qt::Horizontal, ctrl);
    fmPositionSlider->setRange(0, 0);

    fmTimeLabel = new QLabel("--:-- / --:--", ctrl);

    fmMuteBtn = new QPushButton(ctrl);
    fmMuteBtn->setIcon(icoMediaAudio());

    fmVolumeSlider = new QSlider(Qt::Horizontal, ctrl);
    fmVolumeSlider->setRange(0, 100);
    fmVolumeSlider->setValue(50);
    if (fmAudioOutput) fmAudioOutput->setVolume(0.5);

    cr->addWidget(fmPrevFrameBtn);
    cr->addWidget(fmPlayPauseBtn);
    cr->addWidget(fmNextFrameBtn);
    cr->addWidget(fmPositionSlider, 1);
    cr->addWidget(fmTimeLabel);
    cr->addWidget(fmMuteBtn);
    cr->addWidget(fmVolumeSlider);
    pv->addWidget(ctrl);

    // Wire media controls and player signals
    auto fmtTime = [](qint64 ms) {
        if (ms < 0) ms = 0;
        int h = int(ms / 3600000); ms %= 3600000;
        int m = int(ms / 60000);   ms %= 60000;
        int s = int(ms / 1000);
        return h > 0
            ? QString::asprintf("%d:%02d:%02d", h, m, s)
            : QString::asprintf("%02d:%02d", m, s);
    };

    QObject::connect(fmPlayPauseBtn, &QPushButton::clicked, this, [this]() {
        if (m_host && m_host->fmIsSequence) {
            if (m_host->fmSequencePlaying) m_host->pauseFmSequence();
            else m_host->playFmSequence();
            return;
        }
        if (!fmMediaPlayer) return;
        const auto st = fmMediaPlayer->playbackState();
        if (st == QMediaPlayer::PlayingState) {
            fmMediaPlayer->pause();
            fmPlayPauseBtn->setIcon(icoMediaPlay());
        } else {
            fmMediaPlayer->play();
            fmPlayPauseBtn->setIcon(icoMediaPause());
        }
    });

    QObject::connect(fmPrevFrameBtn, &QPushButton::clicked, this, [this]() {
        if (m_host && m_host->fmIsSequence) { m_host->stepFmSequence(-1); return; }
        if (!fmMediaPlayer) return;
        qint64 pos = fmMediaPlayer->position();
        qint64 step = 41; // ~24 fps fallback
        fmMediaPlayer->setPosition(pos > step ? pos - step : 0);
    });

    QObject::connect(fmNextFrameBtn, &QPushButton::clicked, this, [this]() {
        if (m_host && m_host->fmIsSequence) { m_host->stepFmSequence(1); return; }
        if (!fmMediaPlayer) return;
        qint64 pos = fmMediaPlayer->position();
        qint64 step = 41; // ~24 fps fallback
        fmMediaPlayer->setPosition(pos + step);
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderPressed, this, [this]() {
        fmWasPlayingBeforeSeek = false;
        if (m_host && m_host->fmIsSequence) {
            fmWasPlayingBeforeSeek = m_host->fmSequencePlaying;
            if (m_host->fmSequencePlaying) m_host->pauseFmSequence();
        } else if (fmMediaPlayer) {
            fmWasPlayingBeforeSeek = (fmMediaPlayer->playbackState() == QMediaPlayer::PlayingState);
            if (fmWasPlayingBeforeSeek) fmMediaPlayer->pause();
        }
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderMoved, this, [this](int v) {
        if (m_host && m_host->fmIsSequence) {
            m_host->loadFmSequenceFrame(v);
        } else if (fmMediaPlayer) {
            fmMediaPlayer->setPosition(v);
        }
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderReleased, this, [this]() {
        if (m_host && m_host->fmIsSequence) {
            if (fmWasPlayingBeforeSeek) m_host->playFmSequence();
        } else if (fmMediaPlayer) {
            if (fmWasPlayingBeforeSeek) fmMediaPlayer->play();
        }
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::durationChanged, this, [this, fmtTime](qint64 d) {
        if (fmPositionSlider) fmPositionSlider->setRange(0, int(d));
        if (fmTimeLabel) fmTimeLabel->setText(QString("%1 / %2").arg(fmtTime(0), fmtTime(d)));
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::positionChanged, this, [this, fmtTime](qint64 p) {
        if (fmPositionSlider && !fmPositionSlider->isSliderDown()) fmPositionSlider->setValue(int(p));
        if (fmTimeLabel) {
            qint64 d = fmMediaPlayer ? fmMediaPlayer->duration() : 0;
            fmTimeLabel->setText(QString("%1 / %2").arg(fmtTime(p), fmtTime(d)));
        }
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        if (!fmPlayPauseBtn) return;
        fmPlayPauseBtn->setIcon(s == QMediaPlayer::PlayingState ? icoMediaPause() : icoMediaPlay());
    });

    QObject::connect(fmVolumeSlider, &QSlider::valueChanged, this, [this](int v) {
        if (fmAudioOutput) fmAudioOutput->setVolume(qBound(0, v, 100) / 100.0);
        if (fmMuteBtn) fmMuteBtn->setIcon((fmAudioOutput && fmAudioOutput->isMuted()) || v == 0 ? icoMediaMute() : icoMediaAudio());
    });

    QObject::connect(fmMuteBtn, &QPushButton::clicked, this, [this]() {
        if (!fmAudioOutput) return;
        bool to = !fmAudioOutput->isMuted();
        fmAudioOutput->setMuted(to);
        if (fmMuteBtn) fmMuteBtn->setIcon(to ? icoMediaMute() : icoMediaAudio());
    });

    if (!fmSequenceTimer) {
        fmSequenceTimer = new QTimer(fmPreviewPanel);
        fmSequenceTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(fmSequenceTimer, &QTimer::timeout, this, [this]() {
            if (!(m_host && m_host->fmIsSequence)) return;
            int idx = m_host->fmSequenceCurrentIndex + 1;
            if (idx >= m_host->fmSequenceFramePaths.size()) idx = 0;
            m_host->loadFmSequenceFrame(idx);
        });
    }

    // Info panel
    fmInfoPanel = new QWidget(fmRightSplitter);
    fmInfoPanel->setMinimumWidth(260);
    auto info = new QVBoxLayout(fmInfoPanel);
    info->setContentsMargins(8,8,8,8);
    info->setSpacing(4);
    auto makeRow = [&](const char *label, QLabel* &val){
        QWidget *w = new QWidget(fmInfoPanel);
        auto vl = new QVBoxLayout(w); vl->setContentsMargins(0,0,0,0); vl->setSpacing(2);
        auto l = new QLabel(QString::fromLatin1(label), w); l->setStyleSheet("color:#9aa0a6;");
        val = new QLabel("-", w); val->setStyleSheet("color:white;");
        vl->addWidget(l); vl->addWidget(val); info->addWidget(w);
    };
    makeRow("Name", fmInfoFileName);
    makeRow("Path", fmInfoFilePath);
    makeRow("Size", fmInfoFileSize);
    makeRow("Type", fmInfoFileType);
    makeRow("Dimensions", fmInfoDimensions);
    makeRow("Created", fmInfoCreated);
    makeRow("Modified", fmInfoModified);
    makeRow("Permissions", fmInfoPermissions);
    info->addStretch(1);

    // Vertical splitter for Preview | Info
    fmPreviewInfoSplitter = new QSplitter(Qt::Vertical, fmRightSplitter);
    fmPreviewInfoSplitter->addWidget(fmPreviewPanel);
    fmPreviewInfoSplitter->addWidget(fmInfoPanel);
    fmPreviewInfoSplitter->setStretchFactor(0, 2);
    fmPreviewInfoSplitter->setStretchFactor(1, 1);
    fmPreviewInfoSplitter->setChildrenCollapsible(false);
    fmPreviewInfoSplitter->setHandleWidth(6);
    fmPreviewInfoSplitter->setOpaqueResize(true);

    // Assemble and add to layout
    fmRightSplitter->addWidget(viewContainer);
    fmRightSplitter->addWidget(fmPreviewInfoSplitter);
    fmRightSplitter->setStretchFactor(0, 3);
    fmRightSplitter->setStretchFactor(1, 1);
    fmRightSplitter->setChildrenCollapsible(false);
    fmRightSplitter->setHandleWidth(6);
    fmRightSplitter->setOpaqueResize(true);
    rightLayout->addWidget(fmRightSplitter);

    // Restore right splitter states and preview visibility
    {
        QSettings s("AugmentCode", "KAssetManager");
        if (s.contains("FileManager/RightSplitter")) fmRightSplitter->restoreState(s.value("FileManager/RightSplitter").toByteArray());
        if (s.contains("FileManager/PreviewInfoSplitter")) fmPreviewInfoSplitter->restoreState(s.value("FileManager/PreviewInfoSplitter").toByteArray());
        // Apply explicit sizes if available (ensures collapsed panes persist)
        const QVariantList rightSizes = s.value("FileManager/RightSplitterSizes").toList();
        if (!rightSizes.isEmpty()) { QList<int> sz; for (const QVariant &v : rightSizes) sz << v.toInt(); fmRightSplitter->setSizes(sz); }
        const QVariantList pivSizes = s.value("FileManager/PreviewInfoSplitterSizes").toList();
        if (!pivSizes.isEmpty()) { QList<int> sz; for (const QVariant &v : pivSizes) sz << v.toInt(); fmPreviewInfoSplitter->setSizes(sz); }
        const bool previewVisible = s.value("FileManager/PreviewVisible", true).toBool();
        fmPreviewInfoSplitter->setVisible(previewVisible);
    }

    bindHostPointers();
    LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout leave", "DEBUG");
}
