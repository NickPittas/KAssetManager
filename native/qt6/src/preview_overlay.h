#ifndef PREVIEW_OVERLAY_H
#define PREVIEW_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QAudioOutput>
#include <QKeyEvent>
#include <QTimer>
#include <QThread>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsSvgItem>
#include <QGraphicsVideoItem>
#include <QComboBox>
#include <QCheckBox>
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfView>
#endif
#include <QPlainTextEdit>
#include <QIcon>

#include <QTableView>
#include <QStandardItemModel>
#include <QCache>
#include <QMutex>
#include <QThreadPool>
#include <QRunnable>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QSet>
#include <QPointer>
#include <atomic>
#include <QElapsedTimer>


#include "oiio_image_loader.h"

// Forward declarations
class SequenceFrameCache;

class CacheBarWidget;
/**
 * @brief Custom timeline slider with visual cache indicators for image sequences
 *
 * CachedFrameSlider extends QSlider to provide visual feedback about which frames
 * are currently loaded in the RAM cache. This is essential for professional video
 * playback workflows where users need to know which frames can play back smoothly.
 *
 * Visual Design:
 * - Draws a thin red line (3px height) above the timeline groove
 * - Cached frames are shown as continuous red segments
 * - Adjacent cached frames are merged for clean appearance
 * - Matches the visual style of After Effects, Nuke, and DaVinci Resolve
 *
 * Usage:
 * 1. Connect to SequenceFrameCache::frameCached signal
 * 2. Call markFrameCached() when frames are loaded into cache
 * 3. Call clearCachedFrames() when loading a new sequence
 *
 * Technical Details:
 * - Uses QSet for O(1) frame lookup
 * - Sorts frames and merges ranges for efficient painting
 * - Updates automatically via update() calls
 * - Thread-safe when called from main thread (Qt signal/slot mechanism)
 */
class CachedFrameSlider : public QSlider
{
    Q_OBJECT

public:
    explicit CachedFrameSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

    /**
     * @brief Set all cached frames at once (bulk update)
     * @param frames Set of frame indices that are currently cached
     */
    void setCachedFrames(const QSet<int> &frames)
    {
        m_cachedFrames = frames;
        update(); // Trigger repaint
    }

    /**
     * @brief Mark a single frame as cached (incremental update)
     * @param frameIndex The frame index to mark as cached
     *
     * This is called by the SequenceFrameCache::frameCached signal
     * as frames are loaded in the background.
     */
    void markFrameCached(int frameIndex)
    {
        m_cachedFrames.insert(frameIndex);
        update(); // Trigger repaint
    }

    /**
     * @brief Clear all cached frame indicators
     *
     * Should be called when loading a new sequence or clearing the cache.
     */
    void clearCachedFrames()
    {
        m_cachedFrames.clear();
        update(); // Trigger repaint
    }

protected:
    // Custom paint event to draw cached frame indicators
    // Draws a thin red line above the timeline groove showing which frames are in cache
    void paintEvent(QPaintEvent *event) override
    {
        // First, let the base QSlider paint itself
        QSlider::paintEvent(event);

        // Don't draw cache indicators if there are no cached frames
        if (m_cachedFrames.isEmpty() || maximum() <= 0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Get the groove rect (the track area where the slider moves)
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

        // Draw cached frame indicator as a thin red line above the groove
        // This matches the visual style of professional video editing applications
        const int lineHeight = 3; // Height of the cache indicator line
        const int lineY = grooveRect.top() - lineHeight - 2; // Position above groove with 2px gap

        // Calculate the width per frame for positioning
        const int totalFrames = maximum() - minimum() + 1;
        const double pixelsPerFrame = static_cast<double>(grooveRect.width()) / totalFrames;

        // Sort cached frames to find continuous ranges for efficient drawing
        QVector<int> sortedFrames = m_cachedFrames.values();
        std::sort(sortedFrames.begin(), sortedFrames.end());

        if (sortedFrames.isEmpty()) {
            return;
        }

        // Draw cached frames as red line segments
        // Adjacent cached frames are merged into continuous segments for clean appearance
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(220, 50, 50)); // Bright red color

        int rangeStart = sortedFrames[0];
        int rangeEnd = sortedFrames[0];

        for (int i = 1; i <= sortedFrames.size(); ++i) {
            if (i < sortedFrames.size() && sortedFrames[i] == rangeEnd + 1) {
                // Continue the current range
                rangeEnd = sortedFrames[i];
            } else {
                // Draw the current range as a red line segment
                const int startOffset = rangeStart - minimum();
                const int endOffset = rangeEnd - minimum();
                const int x = grooveRect.left() + static_cast<int>(startOffset * pixelsPerFrame);
                const int width = std::max(1, static_cast<int>((endOffset - startOffset + 1) * pixelsPerFrame));

                QRect cacheRect(x, lineY, width, lineHeight);
                painter.drawRect(cacheRect);

                // Start a new range
                if (i < sortedFrames.size()) {
                    rangeStart = sortedFrames[i];
                    rangeEnd = sortedFrames[i];
                }
            }
        }
    }

private:
    QSet<int> m_cachedFrames;
};

// CacheBarWidget: shows cached frames as a thin red line bar (no slider)
class CacheBarWidget : public QWidget {
    Q_OBJECT
public:
    explicit CacheBarWidget(QWidget* parent=nullptr) : QWidget(parent) {
        setFixedHeight(3);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setTotalFrames(int total) {
        m_totalFrames = qMax(0, total);
        update();
    }
    void setCachedFrames(const QSet<int>& frames) {
        m_cached = frames;
        update();
    }
    void markFrameCached(int frameIndex) {
        if (frameIndex >= 0) {
            m_cached.insert(frameIndex);
            update();
        }
    }
    void clearCachedFrames() {
        m_cached.clear();
        update();
    }
protected:
    void paintEvent(QPaintEvent* e) override {
        Q_UNUSED(e);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        // background
        p.fillRect(rect(), QColor(51,51,51));
        if (m_totalFrames <= 0 || m_cached.isEmpty()) return;
        // draw cached segments in red
        const QRect r = rect();
        const double pxPerFrame = static_cast<double>(r.width()) / m_totalFrames;
        QVector<int> sorted = m_cached.values();
        std::sort(sorted.begin(), sorted.end());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(231,76,60));
        int rs = sorted[0];
        int re = sorted[0];
        for (int i=1;i<=sorted.size();++i){
            if (i<sorted.size() && sorted[i] == re+1) {
                re = sorted[i];
            } else {
                int x = r.left() + static_cast<int>(rs * pxPerFrame);
                int w = std::max(1, static_cast<int>((re - rs + 1) * pxPerFrame));
                p.drawRect(QRect(x, r.top(), w, r.height()));
                if (i<sorted.size()) { rs = re = sorted[i]; }
            }
        }
    }
private:
    int m_totalFrames = 0;
    QSet<int> m_cached;
};

class PreviewOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewOverlay(QWidget *parent = nullptr);
    ~PreviewOverlay();

    void showAsset(const QString &filePath, const QString &fileName, const QString &fileType);
    void showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame);
    void navigateNext();
    void navigatePrevious();
    void stopPlayback();
    QString currentPath() const { return currentFilePath; }

signals:
    void closed();
    void navigateRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int position);
    void onSliderPressed();
    void onSliderReleased();
    void onStepPrevFrame();
    void onStepNextFrame();
    void onVolumeChanged(int value);
    void onToggleMute();

    void hideControls();
    void onSequenceTimerTick();
    void onColorSpaceChanged(int index);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onFallbackFrameReady(const QImage &image, qint64 ptsMs);
    void onFallbackFinished();

private:
    void setupUi();
    void showImage(const QString &filePath);
    void showVideo(const QString &filePath);
#ifdef HAVE_QT_PDF
    void showPdf(const QString &filePath);
    void renderPdfPageToImage();
#endif
    void showDoc(const QString &filePath);
    void showDocx(const QString &filePath);
    void showXlsx(const QString &filePath);
    void showText(const QString &filePath);
    void updatePlayPauseButton();
    QString formatTime(qint64 milliseconds);
    void updateVideoTimeDisplays(qint64 positionMs, qint64 durationMs);
    void updateSequenceTimeDisplays(int frameIndex, bool caching=false);
    void zoomImage(double factor);
    void fitImageToView();
    void resetImageZoom();
    void loadSequenceFrame(int frameIndex);
    void positionNavButtons(QWidget* container);
    void playSequence();
    void pauseSequence();
    void stopSequence();
    void startFallbackVideo(const QString &filePath);
    void stopFallbackVideo();
    // Seeking helpers
    double frameDurationMs() const; // based on detectedFps (from metadata) or fallbackFps
    void updateDetectedFps();

    // UI Components
    QGraphicsView *imageView;
    QGraphicsScene *imageScene;
    QGraphicsPixmapItem *imageItem;
    QGraphicsSvgItem *svgItem;
    QGraphicsVideoItem *videoItem;
    QVideoWidget *videoWidget;
    QWidget *controlsWidget;
    QPushButton *playPauseBtn;
    QPushButton *prevFrameBtn;
    QPushButton *nextFrameBtn;
    CacheBarWidget *cacheBar;
    CachedFrameSlider *positionSlider;
    QLabel *currentTimeLabel;
    QLabel *durationTimeLabel;
    QLabel *fpsLabel;
    QSlider *volumeSlider;
    QPushButton *muteBtn;

    QPushButton *closeBtn;
    QLabel *fileNameLabel;
    QComboBox *colorSpaceCombo;
    QLabel *colorSpaceLabel;
    QCheckBox *alphaCheck;
    QPlainTextEdit *textView;

    // Media icons
    QIcon playIcon;
    QIcon pauseIcon;
    QIcon prevFrameIcon;
    QIcon nextFrameIcon;
    QIcon audioIcon;
    QIcon muteIcon;
    QIcon noAudioIcon;

    QTableView *tableView;
    QStandardItemModel *tableModel;

    // Media player
    QMediaPlayer *mediaPlayer;
    QAudioOutput *audioOutput;
#ifdef HAVE_QT_PDF
    QPdfDocument *pdfDoc;
    QPdfView *pdfView;
    int pdfCurrentPage = 0;
#endif

    // Overlay navigation arrows
    QPushButton *navPrevBtn;
    QPushButton *navNextBtn;
    QWidget *navContainer = nullptr; // parent we attach nav buttons to (video widget, image viewport, etc.)

    // State
    QString currentFilePath;
    QString currentFileType;
    bool isVideo;
    QTimer *controlsTimer;

    // Seek/step state
    bool userSeeking = false;
    bool wasPlayingBeforeSeek = false;
    double detectedFps = 0.0;
    // Embedded timecode metadata (if probed via FFmpeg)
    bool hasEmbeddedTimecode = false;
    QString embeddedStartTimecode;

    // Image zoom/pan state
    double currentZoom;
    QPixmap originalPixmap;
    QPoint lastPanPoint;
    bool isPanning;

    // Image sequence playback state
    bool isSequence;
    QStringList sequenceFramePaths;
    int currentSequenceFrame;
    int sequenceStartFrame;
    int sequenceEndFrame;
    QTimer *sequenceTimer;
    bool sequencePlaying;
    SequenceFrameCache *frameCache;
    bool useCacheForSequences; // Flag to enable/disable cache (disabled by default)

    // Fallback (software) video playback state for unsupported codecs (e.g., PNG-in-MOV)
    bool usingFallbackVideo = false;
    class FallbackPngMovReader; // defined in .cpp
    FallbackPngMovReader* fallbackReader = nullptr;
    QThread* fallbackThread = nullptr;
    qint64 fallbackDurationMs = 0;
    double fallbackFps = 0.0;
    bool fallbackPaused = false;

    // Color space for HDR/EXR images
    OIIOImageLoader::ColorSpace currentColorSpace;
    bool isHDRImage;

    // Alpha channel toggle state
    bool alphaOnlyMode = false;
    bool previewHasAlpha = false;

    // Fit-to-window staging flag so we only run expensive fit once
    bool fitPending = false;

    // FPS measurement for sequences
    QElapsedTimer sequenceFpsTimer;
    int sequenceFpsFrames = 0;
    double currentPlaybackFps = 0.0;

    // UI throttling to avoid heavy repaints
    QElapsedTimer uiUpdateTimer; // for slider/time label throttling
    QSize lastFrameSize; // track to avoid resetting scene rect

    // Cache bar update throttle
    QElapsedTimer cacheBarUpdateTimer;

};

// ============================================================================
// SequenceFrameCache: RAM-based pre-fetch cache for image sequence playback
// ============================================================================
class SequenceFrameCache : public QObject
{
    Q_OBJECT

public:
    explicit SequenceFrameCache(QObject *parent = nullptr);
    ~SequenceFrameCache();

    // Cache operations
    void setSequence(const QStringList &framePaths, OIIOImageLoader::ColorSpace colorSpace);
    void clearCache();
    QPixmap getFrame(int frameIndex);
    bool hasFrame(int frameIndex) const;

    // Pre-fetching control
    void startPrefetch(int currentFrame);
    void stopPrefetch();
    void setCurrentFrame(int frameIndex);
    // Tunables
    void setPrefetchConcurrency(int n) { m_prefetchConcurrency = qMax(1, n); }
    int prefetchConcurrency() const { return m_prefetchConcurrency; }

    // Configuration
    void setMaxCacheSize(int maxFrames); // Default: 100 frames
    int maxCacheSize() const { return m_maxCacheSize; }
    int cachedFrameCount() const;
    qint64 currentMemoryUsageMB() const; // Returns current cache memory usage in MB

    // Calculate optimal cache size based on available RAM
    static int calculateOptimalCacheSize(int percentOfFreeRAM = 70);
    static qint64 getAvailableRAM(); // Returns available RAM in MB

    // Cancellation epoch (thread-safe)
    quint64 currentEpoch() const { return m_epoch.load(); }
    bool isEpochCurrent(quint64 epoch) const { return m_epoch.load() == epoch; }

signals:
    void frameCached(int frameIndex);
    void cacheSnapshot(const QSet<int>& frames);

private:
    void prefetchFrames(int startFrame);
    bool isRangeMostlyCached(int start, int end, double threshold) const;
    void scheduleFrameIfNeeded(int frameIndex, quint64 epoch, bool highPriority);
    QPixmap loadFrame(int frameIndex);

    QStringList m_framePaths;
    OIIOImageLoader::ColorSpace m_colorSpace;
    QCache<int, QPixmap> m_cache;
    mutable QRecursiveMutex m_mutex; // Use recursive mutex to allow same thread to lock multiple times
    QThreadPool *m_threadPool;
    int m_maxCacheSize;
    int m_currentFrame;
    bool m_prefetchActive;
    QSet<int> m_pendingFrames; // Track frames being loaded
    std::atomic<quint64> m_epoch; // cancellation epoch; increment to invalidate in-flight workers
    int m_prefetchConcurrency = 4; // default limited concurrency for near-sequential fills

    // Strict sequential sliding window state
    int m_windowStart = 0;
    int m_windowEnd = -1;
    int m_nextToEnqueue = 0;
};

// Worker for loading frames in background
class FrameLoaderWorker : public QObject, public QRunnable
{
    Q_OBJECT

public:
    FrameLoaderWorker(SequenceFrameCache *cache, int frameIndex, const QString &framePath,
                      OIIOImageLoader::ColorSpace colorSpace, quint64 epoch);
    void run() override;

signals:
    void frameLoaded(int frameIndex, QPixmap pixmap);

private:
    QPointer<SequenceFrameCache> m_cache;
    int m_frameIndex;
    QString m_framePath;
    OIIOImageLoader::ColorSpace m_colorSpace;
    quint64 m_epoch;
};

#endif // PREVIEW_OVERLAY_H

