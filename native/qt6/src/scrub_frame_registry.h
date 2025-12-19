#ifndef SCRUB_FRAME_REGISTRY_H
#define SCRUB_FRAME_REGISTRY_H

#include <QObject>
#include <QPixmap>
#include <QModelIndex>
#include <QAbstractItemView>
#include <QHash>
#include <QPair>

/**
 * @brief Singleton registry to store scrubbed frames for grid views.
 * 
 * This allows the item delegate to access scrubbed frames directly,
 * enabling in-place thumbnail scrubbing without overlays.
 */
class ScrubFrameRegistry : public QObject
{
    Q_OBJECT
public:
    static ScrubFrameRegistry& instance();

    /**
     * @brief Set or update the scrub frame for a specific view and model index.
     * @param view The view containing the item
     * @param index The model index being scrubbed
     * @param frame The scrubbed frame pixmap
     * @param position The scrub position (0.0 - 1.0)
     */
    void setScrubFrame(QAbstractItemView *view, const QModelIndex &index, 
                       const QPixmap &frame, qreal position);

    /**
     * @brief Clear the scrub frame for a specific view.
     * @param view The view to clear scrub state for
     */
    void clearScrubFrame(QAbstractItemView *view);

    /**
     * @brief Check if a view has an active scrub frame.
     * @param view The view to check
     * @return true if scrubbing is active for this view
     */
    bool hasScrubFrame(QAbstractItemView *view) const;

    /**
     * @brief Get the scrub data for a view if it exists.
     * @param view The view to query
     * @param outIndex Output: the model index being scrubbed
     * @param outFrame Output: the scrubbed frame
     * @param outPosition Output: the scrub position
     * @return true if scrub data exists
     */
    bool getScrubFrame(QAbstractItemView *view, QModelIndex &outIndex, 
                       QPixmap &outFrame, qreal &outPosition) const;

    /**
     * @brief Check if a specific index in a view is being scrubbed.
     * @param view The view to check
     * @param index The model index to check
     * @return true if this specific index is being scrubbed
     */
    bool isIndexBeingScrubbed(QAbstractItemView *view, const QModelIndex &index) const;

    /**
     * @brief Get the scrub position for a specific index (if scrubbing).
     * @param view The view to check
     * @param index The model index to check
     * @return The scrub position, or -1.0 if not scrubbing
     */
    qreal getScrubPosition(QAbstractItemView *view, const QModelIndex &index) const;

    /**
     * @brief Set the loading state for a view (shows "Decoding..." indicator).
     * @param view The view
     * @param loading Whether a frame is being loaded
     */
    void setLoading(QAbstractItemView *view, bool loading);

    /**
     * @brief Check if a view is loading a scrub frame.
     * @param view The view to check
     * @return true if loading
     */
    bool isLoading(QAbstractItemView *view) const;

signals:
    /**
     * @brief Emitted when a scrub frame is updated.
     * @param view The view that was updated
     * @param index The model index that was updated
     */
    void scrubFrameChanged(QAbstractItemView *view, const QModelIndex &index);

    /**
     * @brief Emitted when scrubbing ends for a view.
     * @param view The view
     * @param index The last scrubbed index
     */
    void scrubEnded(QAbstractItemView *view, const QModelIndex &index);

private:
    ScrubFrameRegistry();
    ~ScrubFrameRegistry() override = default;
    Q_DISABLE_COPY_MOVE(ScrubFrameRegistry)

    struct ScrubData {
        QModelIndex index;
        int row = -1;  // Store row separately for reliable comparison
        QPixmap frame;
        qreal position = 0.0;
        bool loading = false;
    };

    QHash<QAbstractItemView*, ScrubData> m_scrubData;
};

#endif // SCRUB_FRAME_REGISTRY_H
