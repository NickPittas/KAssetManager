#include "fm_item_delegate.h"
#include "live_preview_manager.h"
#include "thumbnail_cache_manager.h"
#include "theme_manager.h"
#include "file_utils.h"
#include "icon_utils.h"
#include <QApplication>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QAbstractProxyModel>
#include <QStyle>

namespace {
    constexpr int kPreviewInset = 1; // minimize border between thumbnail and preview

    // Cached font to avoid construction in paint() - significant performance win
    static const QFont& nameFont() {
        static QFont font("Segoe UI", 9);
        return font;
    }
    
    // Cache the QFontMetrics as well for sizeHint calculations
    static const QFontMetrics& nameFontMetrics() {
        static QFontMetrics fm(nameFont());
        return fm;
    }
    
    // Cached folder icon - standardIcon() is expensive (shell calls on Windows)
    static QIcon& cachedFolderIcon() {
        static QIcon icon;
        if (icon.isNull()) {
            icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        }
        return icon;
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

FmItemDelegate::FmItemDelegate(QObject *parent) 
    : QStyledItemDelegate(parent), m_thumbnailSize(120) 
{
}

void FmItemDelegate::setThumbnailSize(int size) 
{ 
    m_thumbnailSize = size; 
}

int FmItemDelegate::thumbnailSize() const 
{ 
    return m_thumbnailSize; 
}

void FmItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();

    // Outline on hover/selection only (no card fill to minimize borders)
    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;
    // Do not draw background card to keep layout as compact as possible

    if (isSelected || isHovered) {
        QColor c = isSelected ? QColor(88,166,255) : QColor(80,80,80);
        painter->setPen(QPen(c, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(0, 0, -1, -1));
    }

    const int margin = 1; // Minimal margin for tighter layout
    const int thumbSide = m_thumbnailSize;
    QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);
    const QString filePath = index.data(QFileSystemModel::FilePathRole).toString();

    // Check if this is a folder - get the source model through proxy if needed
    // This avoids expensive QFileInfo::isDir() disk I/O on every paint
    const QFileSystemModel *fsModel = nullptr;
    QModelIndex sourceIndex = index;
    
    // Try direct cast first (works if model is QFileSystemModel)
    fsModel = qobject_cast<const QFileSystemModel*>(index.model());
    
    // If that fails, try to get source model through proxy chain
    if (!fsModel) {
        const QAbstractProxyModel *proxy = qobject_cast<const QAbstractProxyModel*>(index.model());
        while (proxy) {
            fsModel = qobject_cast<const QFileSystemModel*>(proxy->sourceModel());
            if (fsModel) {
                sourceIndex = proxy->mapToSource(index);
                break;
            }
            proxy = qobject_cast<const QAbstractProxyModel*>(proxy->sourceModel());
        }
    }
    
    bool isFolder = false;
    if (fsModel && sourceIndex.isValid()) {
        // Use isDir() method which uses cached data - no disk I/O
        isFolder = fsModel->isDir(sourceIndex);
    } else {
        // Last resort fallback - check file path pattern (still avoids disk I/O)
        // Folders typically don't have extensions, but this is imperfect
        isFolder = !filePath.isEmpty() && !filePath.contains(QLatin1Char('.'));
    }

    bool drewPreview = false;

    if (isFolder) {
        // Draw folder icon using cached standard icon (avoids shell calls per-paint)
        const QIcon& folderIcon = cachedFolderIcon();
        QRect iconRect = insetPreviewRect(thumbRect);
        // Scale icon to fit within the preview area with minimal padding
        int iconSize = qMin(iconRect.width(), iconRect.height());
        QRect centeredIconRect(
            iconRect.x() + (iconRect.width() - iconSize) / 2,
            iconRect.y() + (iconRect.height() - iconSize) / 2,
            iconSize,
            iconSize
        );
        folderIcon.paint(painter, centeredIconRect, Qt::AlignCenter);
        drewPreview = true;
    } else {
        // Handle file preview
        // IMPORTANT: Use ThumbnailCacheManager's native size (256x256) to avoid aspect ratio issues
        // Requesting square thumbnails at display size causes double-scaling artifacts
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        ThumbnailCacheManager& cacheManager = ThumbnailCacheManager::instance();
        const QSize targetSize = cacheManager.getThumbnailSize(); // Use cache's native size (256x256)
        
        // Extract suffix from file path directly (avoids QFileInfo construction)
        QString suffix;
        int dotPos = filePath.lastIndexOf(QLatin1Char('.'));
        if (dotPos >= 0 && dotPos < filePath.length() - 1) {
            suffix = filePath.mid(dotPos + 1).toLower();
        }
        const bool previewable = FileUtils::isPreviewableSuffix(suffix);

        if (previewable) {
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
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
                previewMgr.requestFrame(filePath, targetSize);
            }
        }
    }

    if (!drewPreview) {
        // Try to use file type icon - extract suffix from path directly
        QString suffix;
        int dotPos = filePath.lastIndexOf(QLatin1Char('.'));
        if (dotPos >= 0 && dotPos < filePath.length() - 1) {
            suffix = filePath.mid(dotPos + 1);
        }
        QIcon fileIcon = getFileTypeIcon(suffix);

        QRect iconRect = insetPreviewRect(thumbRect);
        // Fit icon within preview area
        int iconSize = qMin(iconRect.width(), iconRect.height());
        QRect centeredIconRect(
            iconRect.x() + (iconRect.width() - iconSize) / 2,
            iconRect.y() + (iconRect.height() - iconSize) / 2,
            iconSize,
            iconSize
        );
        fileIcon.paint(painter, centeredIconRect, Qt::AlignCenter);
    }

    QString name = index.data(Qt::DisplayRole).toString();
    painter->setFont(nameFont());
    painter->setPen(ThemeManager::instance().textColor());
    const int textTop = thumbRect.bottom() + 3; // 3px below image inside the thumbnail
    int textHeight = option.rect.bottom() - textTop; // Use remaining space
    if (textHeight < 24) textHeight = 24; // Ensure at least a couple lines with compact spacing
    QRect nameRect(option.rect.x() + 2, textTop, option.rect.width() - 4, textHeight);

    // Draw text with word wrapping instead of eliding
    painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name);

    painter->restore();
}

QSize FmItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    // Use fixed height for uniform item sizes (much better resize performance)
    // Allow space for 2 lines of wrapped text at most
    int textHeight = nameFontMetrics().lineSpacing() * 2;

    // Total height: thumbnail + gap(3) + text + bottom padding(4)
    int totalHeight = m_thumbnailSize + 3 + textHeight + 4;

    return QSize(m_thumbnailSize + 8, totalHeight);
}
