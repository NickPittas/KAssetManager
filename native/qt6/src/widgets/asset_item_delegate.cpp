#include "widgets/asset_item_delegate.h"

#include "assets_model.h"
#include "live_preview_manager.h"
#include "ui/preview_helpers.h"

#include <QColor>
#include <QDebug>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QPixmap>

AssetItemDelegate::AssetItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
    , m_thumbnailSize(180)
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

void AssetItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    try {
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

        const QString filePath = index.data(AssetsModel::FilePathRole).toString();
        const QString fileType = index.data(AssetsModel::FileTypeRole).toString();

        if (isSelected || isHovered) {
            QColor c = isSelected ? QColor(88,166,255) : QColor(80,80,80);
            painter->setPen(QPen(c, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        }

        const int margin = 6;
        const int thumbSide = m_thumbnailSize;
        QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);

        const QString suffix = QFileInfo(filePath).suffix().toLower();
        LivePreviewManager& previewMgr = LivePreviewManager::instance();
        const QSize targetSize(thumbSide, thumbSide);
        const bool previewable = isPreviewableSuffix(suffix);
        bool drewPreview = false;
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

        if (!drewPreview) {
            painter->setPen(QPen(QColor(120,120,120), 1));
            painter->setBrush(Qt::NoBrush);
            QRect placeholderRect = insetPreviewRect(thumbRect);
            painter->drawRoundedRect(placeholderRect, 6, 6);
            QString label = fileType.toUpper();
            if (label.isEmpty()) label = suffix.toUpper();
            if (label.isEmpty()) label = "FILE";
            QFont placeholder("Segoe UI", 9, QFont::Medium);
            painter->setFont(placeholder);
            painter->setPen(QColor(180,180,180));
            painter->drawText(thumbRect.adjusted(10,10,-10,-10), Qt::AlignCenter | Qt::TextWordWrap, label.left(6));
        }

        bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();
        bool hasGaps = index.data(AssetsModel::SequenceHasGapsRole).toBool();
        if (isSequence && hasGaps) {
            int badgeSize = 24;
            QRect badgeRect(thumbRect.right() - badgeSize - 4, thumbRect.top() + 4, badgeSize, badgeSize);
            painter->setBrush(QColor(255, 140, 0, 200));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(badgeRect);
            painter->setPen(QColor(255, 255, 255));
            QFont badgeFont("Segoe UI", 14, QFont::Bold);
            painter->setFont(badgeFont);
            painter->drawText(badgeRect, Qt::AlignCenter, "!");
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
    } catch (const std::exception& e) {
        qCritical() << "[AssetItemDelegate] Exception in paint():" << e.what();
    } catch (...) {
        qCritical() << "[AssetItemDelegate] Unknown exception in paint()";
    }
}

QSize AssetItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
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
