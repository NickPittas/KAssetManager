#include "asset_item_delegate.h"
#include "assets_model.h"
#include "thumbnail_cache_manager.h"
#include "live_preview_manager.h"
#include "scrub_frame_registry.h"
#include "theme_manager.h"
#include "file_utils.h"
#include <QFileInfo>
#include <QDebug>
#include <QAbstractItemView>

namespace {
    constexpr int kPreviewInset = 1; // minimize border between thumbnail and preview

    // Cached fonts to avoid construction in paint() - significant performance win
    static const QFont& nameFont() {
        static QFont font("Segoe UI", 9);
        return font;
    }
    static const QFont& placeholderFont() {
        static QFont font("Segoe UI", 9, QFont::Medium);
        return font;
    }
    static const QFont& badgeFont() {
        static QFont font("Segoe UI", 10, QFont::Bold);
        return font;
    }
    static const QFont& warningBadgeFont() {
        static QFont font("Segoe UI", 14, QFont::Bold);
        return font;
    }

    QRect insetPreviewRect(const QRect &source)
    {
        QRect result = source.adjusted(kPreviewInset, kPreviewInset, -kPreviewInset, -kPreviewInset);
        if (result.width() <= 0 || result.height() <= 0) {
            return source;
        }
        return result;
    }
}

AssetItemDelegate::AssetItemDelegate(QObject *parent) 
    : QStyledItemDelegate(parent), m_thumbnailSize(180) 
{
}

void AssetItemDelegate::setThumbnailSize(int size) 
{ 
    m_thumbnailSize = size; 
}

int AssetItemDelegate::thumbnailSize() const 
{ 
    return m_thumbnailSize; 
}

void AssetItemDelegate::setView(QAbstractItemView *view)
{
    m_view = view;
}

void AssetItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    try {
        painter->save();

        const bool isSelected = option.state & QStyle::State_Selected;
        const bool isHovered = option.state & QStyle::State_MouseOver;

        const QRect cardRect = option.rect.adjusted(2, 2, -2, -2);
        QColor baseColor(26, 26, 26);   // slightly lighter than #0a0a0a background
        QColor hoverColor(38, 38, 38);
        QColor selectedColor(62, 90, 140);
        QColor cardColor = baseColor;
        if (isSelected) {
            cardColor = selectedColor;
        } else if (isHovered) {
            cardColor = hoverColor;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(cardColor);
        painter->drawRoundedRect(cardRect, 6, 6);

        const QString filePath = index.data(AssetsModel::FilePathRole).toString();
        const QString fileType = index.data(AssetsModel::FileTypeRole).toString();

        if (isSelected || isHovered) {
            QColor c = (option.state & QStyle::State_Selected) ? QColor(88,166,255) : QColor(80,80,80);
            painter->setPen(QPen(c, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        }

        const int margin = 6;
        const int thumbSide = m_thumbnailSize;
        QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);

        const QString suffix = QFileInfo(filePath).suffix().toLower();
        const bool previewable = FileUtils::isPreviewableSuffix(suffix);
        bool drewPreview = false;
        bool usedCachedThumbnail = false;

        // Check if this item is being scrubbed - if so, draw the scrubbed frame instead
        if (m_view && ScrubFrameRegistry::instance().isIndexBeingScrubbed(m_view, index)) {
            QModelIndex scrubIndex;
            QPixmap scrubFrame;
            qreal scrubPosition;
            if (ScrubFrameRegistry::instance().getScrubFrame(m_view, scrubIndex, scrubFrame, scrubPosition)) {
                if (!scrubFrame.isNull()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    QPixmap scaled = scrubFrame.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                }
            }
        }

        // First, try to load from persistent thumbnail cache (if not scrubbing)
        // IMPORTANT: Request thumbnails at the cache's native size (256x256), not the display size
        if (!drewPreview && previewable) {
            ThumbnailCacheManager& cacheManager = ThumbnailCacheManager::instance();
            QSize cacheSize = cacheManager.getThumbnailSize(); // Get the cache's native size (256x256)
            if (cacheManager.isCached(filePath, cacheSize, 0.0)) {
                QPixmap cachedThumb = cacheManager.getCachedThumbnail(filePath, cacheSize, 0.0);
                if (!cachedThumb.isNull()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    // Use FastTransformation for responsive resize - thumbnails are already decent quality
                    QPixmap scaled = cachedThumb.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                    usedCachedThumbnail = true;
                }
            }
        }

        // Fall back to LivePreviewManager if no cached thumbnail
        if (!drewPreview && previewable) {
            ThumbnailCacheManager& cacheManager = ThumbnailCacheManager::instance();
            QSize cacheSize = cacheManager.getThumbnailSize(); // Get the cache's native size (256x256)
            if (cacheManager.isCached(filePath, cacheSize, 0.0)) {
                QPixmap cachedThumb = cacheManager.getCachedThumbnail(filePath, cacheSize, 0.0);
                if (!cachedThumb.isNull()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    // Use FastTransformation for responsive resize - thumbnails are already decent quality
                    QPixmap scaled = cachedThumb.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                    usedCachedThumbnail = true;
                }
            }
        }

        // Fall back to LivePreviewManager if no cached thumbnail
        if (!drewPreview && previewable) {
            LivePreviewManager &previewMgr = LivePreviewManager::instance();
            QSize displaySize(thumbSide, thumbSide); // Use display size for LivePreviewManager
            auto handle = previewMgr.cachedFrame(filePath, displaySize);
            if (handle.isValid()) {
                painter->save();
                QRect previewRect = insetPreviewRect(thumbRect);
                painter->setClipRect(previewRect);
                // Use FastTransformation for responsive resize
                QPixmap scaled = handle.pixmap.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
                int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                painter->drawPixmap(x, y, scaled);
                painter->restore();
                drewPreview = true;
            } else {
                previewMgr.requestFrame(filePath, displaySize);
            }
        }

        if (!drewPreview) {
            painter->setPen(QPen(ThemeManager::instance().borderColor(), 1)); painter->setBrush(Qt::NoBrush);
            QRect placeholderRect = insetPreviewRect(thumbRect);
            painter->drawRoundedRect(placeholderRect, 6, 6);
            QString label = fileType.toUpper();
            if (label.isEmpty()) label = suffix.toUpper();
            if (label.isEmpty()) label = "FILE";
            painter->setFont(placeholderFont());
            painter->setPen(ThemeManager::instance().textColorSecondary());
            painter->drawText(thumbRect.adjusted(10,10,-10,-10), Qt::AlignCenter | Qt::TextWordWrap, label.left(6));
        }

        // Draw cached thumbnail indicator badge in bottom-left corner
        if (drewPreview && usedCachedThumbnail) {
            int badgeSize = 20;
            QRect badgeRect(thumbRect.left() + 4, thumbRect.bottom() - badgeSize - 4, badgeSize, badgeSize);

            // Draw semi-transparent blue background
            painter->setBrush(QColor(88, 166, 255, 180)); // Blue
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(badgeRect);

            // Draw checkmark icon
            painter->setPen(QPen(QColor(255, 255, 255), 2)); // Always white on blue badge
            painter->setFont(badgeFont());
            painter->drawText(badgeRect, Qt::AlignCenter, "✓");
        }

        // Draw warning badge for sequences with gaps
        bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();
        bool hasGaps = index.data(AssetsModel::SequenceHasGapsRole).toBool();
        if (isSequence && hasGaps) {
            int gapCount = index.data(AssetsModel::SequenceGapCountRole).toInt();

            // Draw warning triangle badge in top-right corner
            int badgeSize = 24;
            QRect badgeRect(thumbRect.right() - badgeSize - 4, thumbRect.top() + 4, badgeSize, badgeSize);

            // Draw semi-transparent background
            painter->setBrush(QColor(255, 140, 0, 200)); // Orange
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(badgeRect);

            // Draw warning icon (exclamation mark)
            painter->setPen(QColor(255, 255, 255));
            painter->setFont(warningBadgeFont());
            painter->drawText(badgeRect, Qt::AlignCenter, "!");

            // Tooltip would show: "Sequence has X gap(s)"
            Q_UNUSED(gapCount);
        }

        QString fileName = index.data(AssetsModel::FileNameRole).toString();
        painter->setFont(nameFont());
        painter->setPen(ThemeManager::instance().textColor());
        // Use compact text area: closer to thumbnail with minimal padding
        QRect nameRect(option.rect.x()+2, thumbRect.bottom()+2, option.rect.width()-4, 30);
        QString elided = QFontMetrics(nameFont()).elidedText(fileName, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, elided);
    } catch (const std::exception& e) {
        qCritical() << "[AssetItemDelegate] Exception in paint():" << e.what();
    } catch (...) {
        qCritical() << "[AssetItemDelegate] Unknown exception in paint()";
    }
    painter->restore();
}

QSize AssetItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    int height = m_thumbnailSize + 35; // Compact layout: thumbnail + smaller text area
    return QSize(m_thumbnailSize, height);
}
