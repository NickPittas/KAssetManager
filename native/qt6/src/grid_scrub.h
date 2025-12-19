#ifndef GRID_SCRUB_H
#define GRID_SCRUB_H

#include <QWidget>
#include <QObject>
#include <QPixmap>
#include <QAbstractItemView>
#include <QModelIndex>
#include <QHash>
#include <functional>

/**
 * @brief Minimal overlay for showing scrub progress indicator.
 * 
 * This overlay is now transparent and only draws a progress line
 * on top of the thumbnail. The actual scrubbed frame is drawn
 * by the item delegate using ScrubFrameRegistry.
 */
class GridScrubOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit GridScrubOverlay(QWidget *parent = nullptr);

    void setProgress(qreal value);
    void setLoading(bool loading);
    bool isLoading() const { return m_loading; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_progress = 0.0;
    bool m_loading = false;
};

class GridScrubController : public QObject
{
    Q_OBJECT
public:
    GridScrubController(QAbstractItemView *view,
                        std::function<QString(const QModelIndex&)> resolver,
                        QObject *parent = nullptr);
    ~GridScrubController() override;

    void setSequenceGroupingEnabled(bool enabled);
    bool isSequenceGroupingEnabled() const;
    bool canScrubFile(const QString& filePath) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void handleHoverMove(const QPoint &pos);
    void setCurrentIndex(const QModelIndex &idx);
    void setPosition(qreal value);
    void requestPreview();
    void showOverlay();
    void hideOverlay();
    void updateOverlayGeometry();
    bool handleCtrlScrub(const QPoint &pos);
    QSize currentTargetSize() const;
    QRect thumbRectFor(const QRect &itemRect) const;
    QRect currentThumbRect() const;
    void resetCtrlTracking();
    void beginScrub();
    void endScrub();

    QAbstractItemView *m_view = nullptr;
    std::function<QString(const QModelIndex&)> m_pathResolver;
    GridScrubOverlay *m_overlay = nullptr;
    QModelIndex m_currentIndex;
    QString m_currentPath;
    qreal m_position;
    QHash<QString, qreal> m_positions;
    qreal m_lastMouseX;
    qreal m_requestedPosition = 0.0;  // Track the most recently requested position
    bool m_loadingFrame = false;
    bool m_scrubActive = false;
    bool m_mouseGrabbed = false;
    bool m_warpingCursor = false;
    bool m_sequenceGroupingEnabled = true;
};

#endif // GRID_SCRUB_H
