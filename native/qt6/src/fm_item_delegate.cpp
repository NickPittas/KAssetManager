#include "fm_item_delegate.h"
#include "live_preview_manager.h"
#include "thumbnail_cache_manager.h"
#include "theme_manager.h"
#include "file_utils.h"
#include "icon_utils.h"
#include <QFileInfo>
#include <QFileSystemModel>
#include <QStyle>

namespace {
    constexpr int kPreviewInset = 1; // minimize border between thumbnail and preview

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

    // Check if this is a folder
    QFileInfo fileInfo(filePath);
    const bool isFolder = fileInfo.isDir();

    bool drewPreview = false;

    if (isFolder) {
        // Draw folder icon using Qt's standard folder icon
        QIcon folderIcon = option.widget->style()->standardIcon(QStyle::SP_DirIcon);
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
        const QString suffix = fileInfo.suffix().toLower();
        const bool previewable = FileUtils::isPreviewableSuffix(suffix);

        if (previewable) {
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
            if (handle.isValid()) {
                painter->save();
                QRect previewRect = insetPreviewRect(thumbRect);
                painter->setClipRect(previewRect);
                QPixmap scaled = handle.pixmap.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
        // Try to use file type icon
        QString suffix = fileInfo.suffix();
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
    QFont f("Segoe UI", 9);
    painter->setFont(f);
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
    // Calculate height needed for wrapped text
    QString name = index.data(Qt::DisplayRole).toString();
    QFont f("Segoe UI", 9);
    QFontMetrics fm(f);

    // Calculate text area width (cell width minus margins)
    int textWidth = m_thumbnailSize + 8 - 4; // grid width minus horizontal margins

    // Calculate how many lines the text will need
    QRect boundingRect = fm.boundingRect(QRect(0, 0, textWidth, 1000),
                                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                                         name);
    int textHeight = boundingRect.height();

    // Total height: thumbnail + gap(3) + text + bottom padding(4)
    int totalHeight = m_thumbnailSize + 3 + textHeight + 4;

    return QSize(m_thumbnailSize + 8, totalHeight);
}
