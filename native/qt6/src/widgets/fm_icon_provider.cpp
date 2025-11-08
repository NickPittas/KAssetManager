#include "widgets/fm_icon_provider.h"

#include "live_preview_manager.h"
#include "ui/preview_helpers.h"

#include <QFileInfo>
#include <QSize>

FmIconProvider::FmIconProvider() = default;

QIcon FmIconProvider::icon(const QFileInfo& info) const
{
    if (info.isDir()) {
        return QFileIconProvider::icon(info);
    }

    const QString path = info.absoluteFilePath();
    const QString suffix = info.suffix().toLower();
    if (!isPreviewableSuffix(suffix)) {
        return QFileIconProvider::icon(info);
    }

    const QSize targetSize(64, 64);
    auto handle = LivePreviewManager::instance().cachedFrame(path, targetSize);
    if (handle.isValid()) {
        return QIcon(handle.pixmap);
    }

    LivePreviewManager::instance().requestFrame(path, targetSize);
    return QFileIconProvider::icon(info);
}
