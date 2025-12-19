#include "scrub_frame_registry.h"

ScrubFrameRegistry& ScrubFrameRegistry::instance()
{
    static ScrubFrameRegistry s_instance;
    return s_instance;
}

ScrubFrameRegistry::ScrubFrameRegistry()
    : QObject(nullptr)
{
}

void ScrubFrameRegistry::setScrubFrame(QAbstractItemView *view, const QModelIndex &index,
                                        const QPixmap &frame, qreal position)
{
    if (!view || !index.isValid()) {
        return;
    }

    ScrubData &data = m_scrubData[view];
    
    data.index = index;
    data.row = index.row();
    data.frame = frame;
    data.position = position;
    data.loading = false;

    // Force repaint of the item - use viewport update for reliability
    if (view->viewport()) {
        // Always update the full viewport to ensure the scrubbed frame is drawn
        // This is necessary because QFileSystemModel indices can be tricky
        view->viewport()->update();
    }
    
    emit scrubFrameChanged(view, index);
}

void ScrubFrameRegistry::clearScrubFrame(QAbstractItemView *view)
{
    if (!view) {
        return;
    }

    auto it = m_scrubData.find(view);
    if (it != m_scrubData.end()) {
        QModelIndex lastIndex = it->index;
        int lastRow = it->row;
        m_scrubData.erase(it);
        
        // Trigger repaint for the last scrubbed item
        if (lastIndex.isValid() && view->viewport()) {
            QRect rect = view->visualRect(lastIndex);
            if (rect.isValid()) {
                view->viewport()->update(rect);
            }
            emit scrubEnded(view, lastIndex);
        }
    }
}

bool ScrubFrameRegistry::hasScrubFrame(QAbstractItemView *view) const
{
    auto it = m_scrubData.find(view);
    return it != m_scrubData.end() && it->row >= 0;
}

bool ScrubFrameRegistry::getScrubFrame(QAbstractItemView *view, QModelIndex &outIndex,
                                        QPixmap &outFrame, qreal &outPosition) const
{
    auto it = m_scrubData.find(view);
    if (it == m_scrubData.end() || !it->index.isValid()) {
        return false;
    }

    outIndex = it->index;
    outFrame = it->frame;
    outPosition = it->position;
    return true;
}

bool ScrubFrameRegistry::isIndexBeingScrubbed(QAbstractItemView *view, const QModelIndex &index) const
{
    if (!view || !index.isValid()) {
        return false;
    }

    auto it = m_scrubData.find(view);
    if (it == m_scrubData.end()) {
        return false;
    }

    // Compare by row number - this is more reliable than full QModelIndex comparison
    // for tree models like QFileSystemModel where indices can become stale
    return it->row >= 0 && it->row == index.row();
}

qreal ScrubFrameRegistry::getScrubPosition(QAbstractItemView *view, const QModelIndex &index) const
{
    if (!isIndexBeingScrubbed(view, index)) {
        return -1.0;
    }

    auto it = m_scrubData.find(view);
    return it->position;
}

void ScrubFrameRegistry::setLoading(QAbstractItemView *view, bool loading)
{
    if (!view) {
        return;
    }

    auto it = m_scrubData.find(view);
    if (it != m_scrubData.end()) {
        it->loading = loading;
        if (it->index.isValid()) {
            view->update(view->visualRect(it->index));
        }
    }
}

bool ScrubFrameRegistry::isLoading(QAbstractItemView *view) const
{
    auto it = m_scrubData.find(view);
    return it != m_scrubData.end() && it->loading;
}
