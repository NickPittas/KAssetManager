#ifndef GRID_SCRUB_H
#define GRID_SCRUB_H

#include <QWidget>
#include <QObject>
#include <QPixmap>
#include <QAbstractItemView>
#include <QModelIndex>
#include <QHash>
#include <functional>

class GridScrubOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit GridScrubOverlay(QWidget *parent = nullptr);

    void setProgress(qreal value);
    void setHintText(const QString &text);
    void clearHintText();
    void setFrame(const QPixmap &pixmap);
    void clearFrame();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_progress = 0.0;
    QString m_statusText = QStringLiteral("Ctrl + Move/Wheel to scrub");
    const QString m_defaultHint = QStringLiteral("Ctrl + Move/Wheel to scrub");
    bool m_hasCustomHint = false;
    QPixmap m_frame;
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
    bool m_loadingFrame = false;
    bool m_scrubActive = false;
    bool m_mouseGrabbed = false;
    bool m_warpingCursor = false;
    bool m_sequenceGroupingEnabled = true;
};

#endif // GRID_SCRUB_H
