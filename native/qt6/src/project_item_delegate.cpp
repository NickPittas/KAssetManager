#include "project_item_delegate.h"
#include "project_assets_model.h"
#include "project_version_detector.h"
#include "thumbnail_cache_manager.h"
#include "live_preview_manager.h"
#include "theme_manager.h"
#include "file_utils.h"
#include <QFileInfo>
#include <QComboBox>
#include <QMouseEvent>
#include <QApplication>
#include <QDebug>

namespace {
    constexpr int kPreviewInset = 1;
    constexpr int kVersionBadgeHeight = 22;
    constexpr int kVersionBadgeMinWidth = 50;

    static const QFont& nameFont() {
        static QFont font("Segoe UI", 9);
        return font;
    }
    static const QFont& placeholderFont() {
        static QFont font("Segoe UI", 9, QFont::Medium);
        return font;
    }
    static const QFont& versionFont() {
        static QFont font("Segoe UI", 8);
        return font;
    }
    static const QFont& badgeFont() {
        static QFont font("Segoe UI", 8, QFont::Bold);
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

ProjectItemDelegate::ProjectItemDelegate(QObject *parent) 
    : QStyledItemDelegate(parent), m_thumbnailSize(180) 
{
}

void ProjectItemDelegate::setThumbnailSize(int size) 
{ 
    m_thumbnailSize = size; 
}

int ProjectItemDelegate::thumbnailSize() const 
{ 
    return m_thumbnailSize; 
}

QRect ProjectItemDelegate::getVersionDropdownRect(const QRect& itemRect) const
{
    // Position the version dropdown at bottom-right of thumbnail area
    const int thumbSide = m_thumbnailSize;
    const int margin = 6;
    QRect thumbRect(itemRect.x() + (itemRect.width() - thumbSide) / 2, 
                    itemRect.y() + margin, 
                    thumbSide, thumbSide);
    
    int badgeWidth = qMax(kVersionBadgeMinWidth, thumbSide / 3);
    return QRect(thumbRect.right() - badgeWidth - 4, 
                 thumbRect.bottom() - kVersionBadgeHeight - 4, 
                 badgeWidth, kVersionBadgeHeight);
}

void ProjectItemDelegate::drawVersionBadge(QPainter* painter, const QRect& rect, 
                                           const QString& version, bool hasMultiple, bool hovered) const
{
    painter->save();
    
    // Background color
    QColor bgColor = hasMultiple ? QColor(88, 166, 255, hovered ? 220 : 180) : QColor(60, 60, 60, 180);
    painter->setBrush(bgColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(rect, 4, 4);
    
    // Version text
    painter->setPen(Qt::white);
    painter->setFont(versionFont());
    QString displayText = version.isEmpty() ? "v?" : version;
    if (hasMultiple) {
        displayText += " ▼"; // dropdown indicator
    }
    painter->drawText(rect, Qt::AlignCenter, displayText);
    
    painter->restore();
}

void ProjectItemDelegate::drawProjectTypeBadge(QPainter* painter, const QRect& thumbRect, const QString& filePath) const
{
    painter->save();
    
    int badgeSize = 28;
    QRect badgeRect(thumbRect.left() + 4, thumbRect.top() + 4, badgeSize, badgeSize);
    
    QString suffix = QFileInfo(filePath).suffix().toLower();
    QColor badgeColor;
    QString badgeText;
    
    if (suffix == "aep" || suffix == "aepx") {
        badgeColor = QColor(150, 70, 200); // Purple for AE
        badgeText = "AE";
    } else if (suffix == "nk") {
        badgeColor = QColor(255, 200, 0);  // Yellow for Nuke
        badgeText = "NK";
    } else {
        painter->restore();
        return;
    }
    
    painter->setBrush(badgeColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(badgeRect, 4, 4);
    
    painter->setPen(suffix == "nk" ? Qt::black : Qt::white);
    painter->setFont(badgeFont());
    painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
    
    painter->restore();
}

void ProjectItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    try {
        painter->save();

        const bool isSelected = option.state & QStyle::State_Selected;
        const bool isHovered = option.state & QStyle::State_MouseOver;
        
        // Check if this is a folder entry
        bool isFolder = index.data(ProjectAssetsModel::IsFolderRole).toBool();

        // Card background
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

        // Selection/hover border
        if (isSelected || isHovered) {
            QColor c = isSelected ? QColor(88, 166, 255) : QColor(80, 80, 80);
            painter->setPen(QPen(c, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        }

        // Thumbnail area
        const int margin = 6;
        const int thumbSide = m_thumbnailSize;
        QRect thumbRect(option.rect.x() + (option.rect.width() - thumbSide) / 2, 
                        option.rect.y() + margin, thumbSide, thumbSide);

        // Handle folder display
        if (isFolder) {
            // Draw folder icon placeholder
            painter->setPen(QPen(ThemeManager::instance().borderColor(), 1));
            painter->setBrush(Qt::NoBrush);
            QRect placeholderRect = insetPreviewRect(thumbRect);
            painter->drawRoundedRect(placeholderRect, 6, 6);
            
            // Draw folder icon using system style
            QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
            int iconSize = thumbSide / 2;
            QRect iconRect(thumbRect.center().x() - iconSize / 2, 
                           thumbRect.center().y() - iconSize / 2, 
                           iconSize, iconSize);
            folderIcon.paint(painter, iconRect);
            
            // File name (folder name)
            QString folderName = index.data(ProjectAssetsModel::FileNameRole).toString();
            painter->setFont(nameFont());
            painter->setPen(ThemeManager::instance().textColor());
            QRect nameRect(option.rect.x() + 2, thumbRect.bottom() + 2, option.rect.width() - 4, 30);
            QString elided = QFontMetrics(nameFont()).elidedText(folderName, Qt::ElideRight, nameRect.width());
            painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, elided);
            
            painter->restore();
            return;  // Done with folder rendering
        }

        const QString filePath = index.data(ProjectAssetsModel::FilePathRole).toString();
        const QString fileType = index.data(ProjectAssetsModel::FileTypeRole).toString();

        const QString suffix = QFileInfo(filePath).suffix().toLower();
        const bool previewable = FileUtils::isPreviewableSuffix(suffix);
        bool drewPreview = false;
        bool usedCachedThumbnail = false;

        // Try cached thumbnail first
        if (previewable) {
            ThumbnailCacheManager& cacheManager = ThumbnailCacheManager::instance();
            QSize cacheSize = cacheManager.getThumbnailSize();
            if (cacheManager.isCached(filePath, cacheSize, 0.0)) {
                QPixmap cachedThumb = cacheManager.getCachedThumbnail(filePath, cacheSize, 0.0);
                if (!cachedThumb.isNull()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    QPixmap scaled = cachedThumb.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                    usedCachedThumbnail = true;
                }
            }
        }

        // Fall back to LivePreviewManager
        if (!drewPreview && previewable) {
            LivePreviewManager &previewMgr = LivePreviewManager::instance();
            QSize displaySize(thumbSide, thumbSide);
            auto handle = previewMgr.cachedFrame(filePath, displaySize);
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
                previewMgr.requestFrame(filePath, displaySize);
            }
        }

        // Placeholder if no preview
        if (!drewPreview) {
            painter->setPen(QPen(ThemeManager::instance().borderColor(), 1));
            painter->setBrush(Qt::NoBrush);
            QRect placeholderRect = insetPreviewRect(thumbRect);
            painter->drawRoundedRect(placeholderRect, 6, 6);
            
            QString label = fileType.toUpper();
            if (label.isEmpty()) label = suffix.toUpper();
            if (label.isEmpty()) label = "FILE";
            painter->setFont(placeholderFont());
            painter->setPen(ThemeManager::instance().textColorSecondary());
            painter->drawText(thumbRect.adjusted(10, 10, -10, -10), Qt::AlignCenter | Qt::TextWordWrap, label.left(6));
        }

        if (drewPreview && usedCachedThumbnail) {
            int badgeSize = 20;
            QRect badgeRect(thumbRect.left() + 4, thumbRect.bottom() - badgeSize - 4, badgeSize, badgeSize);

            painter->setBrush(QColor(88, 166, 255, 180));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(badgeRect);

            painter->setPen(QPen(Qt::white, 2));
            painter->setFont(badgeFont());
            painter->drawText(badgeRect, Qt::AlignCenter, "û");
        }

        // Draw project type badge (AE/NK) in top-left
        bool isProjectFile = ProjectVersionDetector::isProjectFile(filePath);
        if (isProjectFile) {
            drawProjectTypeBadge(painter, thumbRect, filePath);
        }

        // Draw version badge for items with multiple versions
        bool hasMultipleVersions = index.data(ProjectAssetsModel::HasMultipleVersionsRole).toBool();
        QString versionString = index.data(ProjectAssetsModel::VersionStringRole).toString();
        if (isProjectFile && !versionString.isEmpty()) {
            QRect versionRect = getVersionDropdownRect(option.rect);
            bool versionHovered = isHovered && versionRect.contains(m_lastMousePos);
            drawVersionBadge(painter, versionRect, versionString, hasMultipleVersions, versionHovered);
        }

        // File name
        QString fileName = index.data(ProjectAssetsModel::FileNameRole).toString();
        painter->setFont(nameFont());
        painter->setPen(ThemeManager::instance().textColor());
        QRect nameRect(option.rect.x() + 2, thumbRect.bottom() + 2, option.rect.width() - 4, 30);
        QString elided = QFontMetrics(nameFont()).elidedText(fileName, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, elided);

        painter->restore();
    } catch (const std::exception& e) {
        qCritical() << "[ProjectItemDelegate] Exception in paint():" << e.what();
        painter->restore();
    }
}

QSize ProjectItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    // Item size: thumbnail + margins + text area
    return QSize(m_thumbnailSize + 24, m_thumbnailSize + 45);
}

bool ProjectItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, 
                                       const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        m_lastMousePos = mouseEvent->pos();
    }
    
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        
        bool hasMultipleVersions = index.data(ProjectAssetsModel::HasMultipleVersionsRole).toBool();
        if (hasMultipleVersions) {
            QRect versionRect = getVersionDropdownRect(option.rect);
            if (versionRect.contains(mouseEvent->pos())) {
                // Show version dropdown - handled by view
                return true; // Event handled
            }
        }
    }
    
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QWidget* ProjectItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, 
                                           const QModelIndex& index) const
{
    Q_UNUSED(option);
    
    bool hasMultipleVersions = index.data(ProjectAssetsModel::HasMultipleVersionsRole).toBool();
    if (!hasMultipleVersions) {
        return nullptr;
    }
    
    QComboBox* combo = new QComboBox(parent);
    combo->setStyleSheet(ThemeManager::instance().comboBoxStyleSheet());
    
    // Get version list
    QStringList versions = index.data(ProjectAssetsModel::VersionListRole).toStringList();
    for (const QString& v : versions) {
        combo->addItem(v);
    }
    
    return combo;
}

void ProjectItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    
    QString currentVersion = index.data(ProjectAssetsModel::VersionStringRole).toString();
    int idx = combo->findText(currentVersion);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
}

void ProjectItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, 
                                       const QModelIndex& index) const
{
    Q_UNUSED(model);
    
    QComboBox* combo = qobject_cast<QComboBox*>(editor);
    if (!combo) return;
    
    QString selectedVersion = combo->currentText();
    qint64 assetId = index.data(ProjectAssetsModel::IdRole).toLongLong();
    QString groupKey = index.data(ProjectAssetsModel::VersionGroupKeyRole).toString();
    
    // Emit signal for version change - the view/model will handle the actual update
    // For now, we just need to find the path for the selected version
    // This would be handled by connecting to a version lookup in the model
    emit const_cast<ProjectItemDelegate*>(this)->versionSelected(assetId, selectedVersion);
}

void ProjectItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, 
                                               const QModelIndex& index) const
{
    Q_UNUSED(index);
    
    // Position the dropdown editor at the version badge location
    QRect versionRect = getVersionDropdownRect(option.rect);
    
    // Expand combo box to be usable
    int comboWidth = qMax(120, versionRect.width());
    QRect editorRect(versionRect.x(), versionRect.y(), comboWidth, versionRect.height() + 4);
    editor->setGeometry(editorRect);
}
