#include "widgets/fm_item_delegate.h"

#include "live_preview_manager.h"
#include "ui/icon_helpers.h"
#include "ui/preview_helpers.h"

#include <QColor>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QStyle>

FmItemDelegate::FmItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
    , m_thumbnailSize(120)
{
}

void FmItemDelegate::setThumbnailSize(int s)
{
    m_thumbnailSize = s;
}

int FmItemDelegate::thumbnailSize() const
{
    return m_thumbnailSize;
}

void FmItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    painter->save();

    const bool isSelected = option.state & QStyle::State_Selected;
    const bool isHovered = option.state & QStyle::State_MouseOver;
    const QRect cardRect = option.rect.adjusted(2, 2, -2, -2);
    QColor baseColor(26, 26, 26);
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

    if (isSelected || isHovered) {
        QColor c = isSelected ? QColor(88,166,255) : QColor(80,80,80);
        painter->setPen(QPen(c, 1.5));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
    }

    const int margin = 6;
    const int thumbSide = m_thumbnailSize;
    QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);
    const QString filePath = index.data(QFileSystemModel::FilePathRole).toString();

    QFileInfo fileInfo(filePath);
    const bool isFolder = fileInfo.isDir();

    bool drewPreview = false;

    if (isFolder) {
        QIcon folderIcon = option.widget->style()->standardIcon(QStyle::SP_DirIcon);
        QRect iconRect = insetPreviewRect(thumbRect);
        int iconSize = qMin(iconRect.width(), iconRect.height()) * 0.8;
        QRect centeredIconRect(
            iconRect.x() + (iconRect.width() - iconSize) / 2,
            iconRect.y() + (iconRect.height() - iconSize) / 2,
            iconSize,
            iconSize
        );
        folderIcon.paint(painter, centeredIconRect, Qt::AlignCenter);
        drewPreview = true;
    } else {
        LivePreviewManager& previewMgr = LivePreviewManager::instance();
        const QSize targetSize(thumbSide, thumbSide);
        const QString suffix = fileInfo.suffix().toLower();
        const bool previewable = isPreviewableSuffix(suffix);

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
            }
        }
    }

    if (!drewPreview) {
        QString suffix = fileInfo.suffix();
        QIcon fileIcon = getFileTypeIcon(suffix);

        QRect iconRect = insetPreviewRect(thumbRect);
        int iconSize = qMin(iconRect.width(), iconRect.height()) * 0.6;
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
    painter->setPen(QColor(230,230,230));
    const int textTop = thumbRect.bottom() - 2;
    int textHeight = option.rect.bottom() - textTop;
    if (textHeight < 35) textHeight = 35;
    QRect nameRect(option.rect.x() + 4, textTop, option.rect.width() - 8, textHeight);
    painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, name);

    painter->restore();
}

QSize FmItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(option);
    QString name = index.data(Qt::DisplayRole).toString();
    QFont f("Segoe UI", 9);
    QFontMetrics fm(f);
    int textWidth = m_thumbnailSize + 24 - 8;
    QRect boundingRect = fm.boundingRect(QRect(0, 0, textWidth, 1000),
                                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                                         name);
    int textHeight = boundingRect.height();
    int totalHeight = m_thumbnailSize + 6 - 2 + textHeight + 10;
    return QSize(m_thumbnailSize + 24, totalHeight);
}
