#pragma once

#include <QObject>
#include <QHash>
#include <QModelIndex>
#include <QPoint>
#include <QString>
#include <QRect>
#include <QSize>

#include <functional>
#include <limits>

class GridScrubOverlay;
class QAbstractItemView;
class QScrollBar;

class GridScrubController : public QObject
{
    Q_OBJECT

public:
    GridScrubController(QAbstractItemView* view,
                        std::function<QString(const QModelIndex&)> resolver,
                        QObject* parent = nullptr);
    ~GridScrubController() override;

    void setSequenceGroupingEnabled(bool enabled);
    bool isSequenceGroupingEnabled() const;

    bool canScrubFile(const QString& filePath) const;

    void setCurrentIndex(const QModelIndex& idx);
    void setPosition(qreal value);
    void requestPreview();
    void showOverlay();
    void hideOverlay();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateOverlayGeometry();
    bool handleCtrlScrub(const QPoint& pos);
    QSize currentTargetSize() const;
    QRect thumbRectFor(const QRect& itemRect) const;
    QRect currentThumbRect() const;
    void resetCtrlTracking();
    void beginScrub();
    void endScrub();

    QAbstractItemView* m_view = nullptr;
    std::function<QString(const QModelIndex&)> m_pathResolver;
    GridScrubOverlay* m_overlay = nullptr;
    QModelIndex m_currentIndex;
    QString m_currentPath;
    qreal m_position = 0.0;
    QHash<QString, qreal> m_positions;
    qreal m_lastMouseX = std::numeric_limits<qreal>::quiet_NaN();
    bool m_loadingFrame = false;
    bool m_scrubActive = false;
    bool m_mouseGrabbed = false;
    bool m_warpingCursor = false;
    bool m_sequenceGroupingEnabled = true;
};
