#ifndef PREVIEW_OVERLAY_H
#define PREVIEW_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QKeyEvent>
#include <QTimer>
#include <QThread>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QGraphicsSvgItem>
#include <QComboBox>
#include <QCheckBox>
#include <QJsonArray>
#include "annotation_layer.h"
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
#include <QPixmap>
#include <QPainter>
#include <QStyleOptionSlider>
#include <QSet>
#include <QPointer>
#include <atomic>
#include <QElapsedTimer>
#include <cmath>


#include "oiio_image_loader.h"
#include "sequence_detector.h"
#include "media/player_lab_player.h"

#include "media/player_lab_viewport.h"

// Forward declarations
class SequenceFrameCache;
class QVBoxLayout;

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
        setMouseTracking(true);
    }

    /**
     * @brief Set context for drawing timeline guides
     * @param isVideo True when slider units are milliseconds, false when units are frames
     * @param fps Frames per second used to derive 1-second markers
     */
    void setTimelineContext(bool isVideo, double fps)
    {
        m_isVideo = isVideo;
        m_fps = (fps > 0.0) ? fps : 24.0;
        invalidateStaticTimeline();
    }

    /**
     * @brief Set all cached frames at once (bulk update)
     * @param frames Set of frame indices that are currently cached
     */
    void setCachedFrames(const QSet<int> &frames)
    {
        m_cachedFrames = frames;
        invalidateStaticTimeline();
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
        invalidateStaticTimeline();
    }

    /**
     * @brief Clear all cached frame indicators
     *
     * Should be called when loading a new sequence or clearing the cache.
     */
    void clearCachedFrames()
    {
        m_cachedFrames.clear();
        invalidateStaticTimeline();
    }
    
    /**
     * @brief Set annotated frames (bulk update)
     * @param frames Set of frame indices that have annotations
     */
    void setAnnotatedFrames(const QSet<int> &frames)
    {
        m_annotatedFrames = frames;
        invalidateStaticTimeline();
    }
    
    /**
     * @brief Mark a single frame as annotated
     * @param frameIndex The frame index to mark as annotated
     */
    void markFrameAnnotated(int frameIndex)
    {
        m_annotatedFrames.insert(frameIndex);
        invalidateStaticTimeline();
    }
    
    /**
     * @brief Unmark a frame as annotated
     * @param frameIndex The frame index to remove annotation marker
     */
    void unmarkFrameAnnotated(int frameIndex)
    {
        m_annotatedFrames.remove(frameIndex);
        invalidateStaticTimeline();
    }
    
    /**
     * @brief Clear all annotation indicators
     */
    void clearAnnotatedFrames()
    {
        m_annotatedFrames.clear();
        invalidateStaticTimeline();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const int newValue = pixelPosToRangeValue(event->position().toPoint());
            setSliderDown(true);
            setValue(newValue);
            emit sliderPressed();
            emit sliderMoved(newValue);
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (isSliderDown() && (event->buttons() & Qt::LeftButton)) {
            const int newValue = pixelPosToRangeValue(event->position().toPoint());
            setValue(newValue);
            emit sliderMoved(newValue);
            event->accept();
            return;
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && isSliderDown()) {
            const int newValue = pixelPosToRangeValue(event->position().toPoint());
            setValue(newValue);
            setSliderDown(false);
            emit sliderReleased();
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

    // Custom paint event to draw cached frame indicators
    // Draws a thin red line above the timeline groove showing which frames are in cache
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const QRectF timelineRect = timelineRectForCurrentStyle();
        ensureStaticTimelineCache(timelineRect);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawPixmap(0, 0, m_staticTimelineCache);
        drawPlayhead(painter, timelineRect);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QSlider::resizeEvent(event);
        invalidateStaticTimeline();
    }

    void sliderChange(SliderChange change) override
    {
        const QRect oldPlayhead = playheadUpdateRect();
        QSlider::sliderChange(change);
        if (change == SliderValueChange) {
            update(oldPlayhead.united(playheadUpdateRect()));
        } else if (change == SliderRangeChange || change == SliderOrientationChange) {
            invalidateStaticTimeline();
        }
    }

private:
    void invalidateStaticTimeline()
    {
        m_staticTimelineDirty = true;
        update();
    }

    QRectF timelineRectForCurrentStyle() const
    {
        QStyleOptionSlider opt;
        const_cast<CachedFrameSlider*>(this)->initStyleOption(&opt);
        const QRect grooveRect = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const int timelineHeight = 26;
        const int centerY = grooveRect.center().y();
        QRectF timelineRect(grooveRect.left(),
                            centerY - timelineHeight / 2.0,
                            grooveRect.width(),
                            timelineHeight);
        return timelineRect.intersected(rect());
    }

    void ensureStaticTimelineCache(const QRectF& timelineRect)
    {
        if (!m_staticTimelineDirty && m_staticTimelineCache.size() == size()) {
            return;
        }

        m_staticTimelineCache = QPixmap(size());
        m_staticTimelineCache.fill(Qt::transparent);

        QPainter painter(&m_staticTimelineCache);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(30, 30, 32));
        painter.drawRoundedRect(timelineRect, 5, 5);
        painter.setBrush(QColor(255, 255, 255, 10));
        painter.drawRoundedRect(timelineRect.adjusted(1, 1, -1, -1), 5, 5);
        painter.setPen(QPen(QColor(70, 70, 75), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(timelineRect.adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);

        const double totalUnits = static_cast<double>(maximum() - minimum());
        const double unitsPerSecond = m_isVideo ? 1000.0 : qMax(1.0, m_fps);

        if (totalUnits > 0.0) {
            const double totalSeconds = totalUnits / unitsPerSecond;
            const int stripeCount = static_cast<int>(std::ceil(totalSeconds));
            const QColor stripeLight(255, 255, 255, 18);
            const QColor stripeDark(255, 255, 255, 6);
            const QColor secondLine(255, 255, 255, 60);

            for (int sec = 0; sec < stripeCount; ++sec) {
                const double startUnit = minimum() + (sec * unitsPerSecond);
                const double endUnit = qMin<double>(
                    minimum() + ((sec + 1) * unitsPerSecond), maximum());
                const double startX = timelineRect.left() +
                    ((startUnit - minimum()) / totalUnits) * timelineRect.width();
                const double endX = timelineRect.left() +
                    ((endUnit - minimum()) / totalUnits) * timelineRect.width();

                const QRectF stripeRect(startX,
                                        timelineRect.top() + 1,
                                        qMax(1.0, endX - startX),
                                        timelineRect.height() - 2);
                painter.fillRect(stripeRect, (sec % 2 == 0) ? stripeLight : stripeDark);

                const double tickX = qMin(endX, timelineRect.right());
                painter.setPen(QPen(secondLine, 1));
                painter.drawLine(QPointF(tickX, timelineRect.top() + 1),
                                 QPointF(tickX, timelineRect.bottom() - 1));
            }
        }

        drawSegments(painter, timelineRect, m_cachedFrames,
                     QColor(220, 50, 50, 200), 4, 4);
        drawSegments(painter, timelineRect, m_annotatedFrames,
                     QColor(50, 220, 100, 210), 4,
                     static_cast<int>(timelineRect.height()) - 8);

        m_staticTimelineDirty = false;
    }

    void drawSegments(QPainter& painter,
                      const QRectF& timelineRect,
                      const QSet<int>& values,
                      const QColor& color,
                      int barHeight,
                      int topPadding) const
    {
        const double totalUnits = static_cast<double>(maximum() - minimum());
        if (values.isEmpty() || totalUnits <= 0.0) {
            return;
        }

        QVector<int> sorted = values.values();
        std::sort(sorted.begin(), sorted.end());

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);

        int rangeStart = sorted[0];
        int rangeEnd = sorted[0];
        for (int i = 1; i <= sorted.size(); ++i) {
            if (i < sorted.size() && sorted[i] == rangeEnd + 1) {
                rangeEnd = sorted[i];
                continue;
            }

            const double startX = timelineRect.left() +
                ((rangeStart - minimum()) / totalUnits) * timelineRect.width();
            const double endX = timelineRect.left() +
                ((rangeEnd - minimum()) / totalUnits) * timelineRect.width();
            const QRectF segment(startX,
                                 timelineRect.top() + topPadding,
                                 qMax(2.0, endX - startX),
                                 barHeight);
            painter.drawRoundedRect(segment, 2, 2);

            if (i < sorted.size()) {
                rangeStart = sorted[i];
                rangeEnd = sorted[i];
            }
        }
    }

    double playheadX(const QRectF& timelineRect) const
    {
        const double totalUnits = static_cast<double>(maximum() - minimum());
        if (totalUnits <= 0.0) {
            return timelineRect.left();
        }
        return timelineRect.left() +
            ((value() - minimum()) / totalUnits) * timelineRect.width();
    }

    QRect playheadUpdateRect() const
    {
        const QRectF timelineRect = timelineRectForCurrentStyle();
        const double x = playheadX(timelineRect);
        return QRect(static_cast<int>(std::floor(x)) - 8,
                     static_cast<int>(std::floor(timelineRect.top())) - 10,
                     17,
                     static_cast<int>(std::ceil(timelineRect.height())) + 20)
            .intersected(rect());
    }

    void drawPlayhead(QPainter& painter, const QRectF& timelineRect) const
    {
        const double x = playheadX(timelineRect);
        const QColor playheadColor(255, 125, 85);
        painter.setPen(QPen(playheadColor, 2));
        painter.drawLine(QPointF(x, timelineRect.top() - 6),
                         QPointF(x, timelineRect.bottom() + 6));

        painter.setBrush(playheadColor);
        painter.setPen(Qt::NoPen);
        const QVector<QPointF> head = {
            QPointF(x, timelineRect.top() - 8),
            QPointF(x - 6, timelineRect.top() - 1),
            QPointF(x + 6, timelineRect.top() - 1)
        };
        painter.drawPolygon(head.constData(), head.size());
    }

    int pixelPosToRangeValue(const QPoint& pos) const
    {
        QStyleOptionSlider opt;
        const_cast<CachedFrameSlider*>(this)->initStyleOption(&opt);
        const QRect grooveRect = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const int span = qMax(1, grooveRect.width() - 1);
        const int x = qBound(grooveRect.left(), pos.x(), grooveRect.right());
        const double t = static_cast<double>(x - grooveRect.left()) / span;
        return minimum() + qRound(t * (maximum() - minimum()));
    }

    QPixmap m_staticTimelineCache;
    bool m_staticTimelineDirty = true;
    bool m_isVideo = true;
    double m_fps = 24.0;
    QSet<int> m_cachedFrames;
    QSet<int> m_annotatedFrames;
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
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
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

    // PlayerLabPlayer signal handlers
    void onPlayerPositionChanged(qint64 positionMs);
    void onPlayerDurationChanged(qint64 durationMs);
    void onPlayerMediaInfo(const PlayerLabPlayer::MediaInfo& info);
    void onPlayerPlaybackStateChanged(PlayerLabPlayer::PlaybackState state);
    void onPlayerError(const QString& errorString);
    void onPlayerEndOfStream();
    
    // Annotation slots
    void onToggleAnnotation();
    void onAnnotationToolSelected();
    void onColorPicker();
    void onPenWidthChanged(int width);
    void onClearAnnotations();
    void onSaveAnnotatedFrame();
    void onSaveAllAnnotatedFrames();

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
    // Seeking helpers
    double frameDurationMs() const; // based on detectedFps (from metadata) or fallbackFps
    void updateDetectedFps();
    void setPlaybackControlsVisible(bool visible);
    void setControlsHeightForImage(bool imageMode);
    void setControlsVisible(bool visible);
    void requestVideoScrubSeek(qint64 positionMs);
    void issueVideoScrubSeek(qint64 positionMs);
    void completeVideoScrubSeek(qint64 presentedPositionMs);
    QWidget* activeVideoWidget() const;
    void hideVideoWidgets();
    void toggleStillImageFit();
    void fitStillImageToView();
    void resetStillImageZoom();
    void zoomStillImage(double factor);
    
    // Annotation helpers
    void setupAnnotationToolbar();
    void enableAnnotationMode(bool enable);
    void resetAnnotationSession();
    void saveCurrentFrameAnnotations();
    void loadFrameAnnotations(int frameIndex);
    QImage captureCurrentFrame();
    void exportAnnotatedFrame(const QString& filePath, const QString& format);
    void updateVideoAnnotationFrame(); // Update frame and handle per-frame annotations for videos
    int getVideoFrameNumber() const; // Calculate frame number from current position and FPS

    void ensurePlayer();
    void ensureRenderWidget();

    // UI Components
    QGraphicsView *imageView;
    QGraphicsScene *imageScene;
    QGraphicsPixmapItem *imageItem;
    QGraphicsView *stillImageView = nullptr;
    QGraphicsSvgItem *svgItem;
    QWidget *videoWidget; // Video rendering placeholder (replaced by tlRender viewport)
    QWidget *controlsWidget;
    QPushButton *playPauseBtn;
    QPushButton *prevFrameBtn;
    QPushButton *nextFrameBtn;
    CacheBarWidget *cacheBar;
    CachedFrameSlider *positionSlider;
    QLabel *currentTimeLabel;
    QLabel *durationTimeLabel;
    QLabel *fpsLabel;
    QVBoxLayout *controlsLayout = nullptr;
    QSlider *volumeSlider;
    QPushButton *muteBtn;

    QPushButton *closeBtn;
    QLabel *fileNameLabel;

    // Playback controls
    QComboBox *loopModeCombo = nullptr;
    QComboBox *playbackRateCombo = nullptr;

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

    // PlayerLab media player / raster viewport
    PlayerLabPlayer *m_player;
    PlayerLabViewport *m_renderWidget;
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
    bool m_tearingDown = false;
    QTimer *controlsTimer;

    // Seek/step state
    bool userSeeking = false;
    bool wasPlayingBeforeSeek = false;
    bool controlsPinned = false;
    bool controlsHovering = false;
    double detectedFps = 0.0;
    qint64 lastKnownPosition = -1; // Store position for display during pause
    int lastKnownVideoFrame = -1; // Explicitly tracked frame number for annotation accuracy
    QElapsedTimer playerPositionUiThrottle;
    qint64 pendingPlayerPositionMs = -1;
    bool videoScrubSeekInFlight = false;
    bool resumePlaybackAfterVideoScrub = false;
    qint64 pendingVideoScrubSeekMs = -1;
    // Monotonic scrub-seek generation. Bumped on file change / stop so any
    // late position callback from the previous clip cannot re-arm a stale
    // scrub-seek chain into the new file.
    quint64 videoScrubSeekSerial = 0;
    // Serial captured when issueVideoScrubSeek() fired; completeVideoScrubSeek
    // ignores completions whose serial does not match (stale callback from a
    // previous file / before a stop).
    quint64 videoScrubSeekIssuedSerial = 0;
    // Embedded timecode metadata (if probed via FFmpeg)
    bool hasEmbeddedTimecode = false;
    QString embeddedStartTimecode;

    // Image zoom/pan state
    double currentZoom;
    double stillImageZoom = 1.0;
    QPixmap originalPixmap;
    bool stillFitMode = true;
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
    int sequencePlayDirection; // 1 = forward, -1 = reverse (for JKL scrubbing)
    SequenceFrameCache *frameCache;
    bool useCacheForSequences; // Flag to enable/disable cache (disabled by default)

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
    
    // Annotation system
    AnnotationLayer* annotationLayer;
    bool annotationModeEnabled;
    QWidget* annotationToolbar;
    
    // Annotation overlay for videos (transparent layer on top of videoWidget)
    QGraphicsView* annotationOverlayView;
    QGraphicsScene* annotationOverlayScene;
    QPushButton* toggleAnnotationBtn;
    QPushButton* selectToolBtn;
    QPushButton* penToolBtn;
    QPushButton* textToolBtn;
    QPushButton* rectangleToolBtn;
    QPushButton* ellipseToolBtn;
    QPushButton* arrowToolBtn;
    QPushButton* colorPickerBtn;
    QSlider* penWidthSlider;
    QPushButton* clearAnnotationsBtn;
    QPushButton* saveFrameBtn;
    QPushButton* saveAllFramesBtn;
    QPushButton* undoBtn;
    QPushButton* redoBtn;
    
    // Per-frame annotation storage for sequences/videos (using JSON serialization)
    QMap<int, QJsonArray> frameAnnotations;
    QSet<int> annotatedFrameIndices;
    int currentAnnotatedFrame;

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
    void setSequence(const QStringList &framePaths);
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
                      quint64 epoch);
    void run() override;

signals:
    void frameLoaded(int frameIndex, QImage image);

private:
    QPointer<SequenceFrameCache> m_cache;
    int m_frameIndex;
    QString m_framePath;
    quint64 m_epoch;
};

#endif // PREVIEW_OVERLAY_H
