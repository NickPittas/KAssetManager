#include "ui/preview_helpers.h"

#include <QSet>

namespace {
constexpr int kPreviewInset = 8;
}

QRect insetPreviewRect(const QRect& source)
{
    QRect result = source.adjusted(kPreviewInset, kPreviewInset, -kPreviewInset, -kPreviewInset);
    if (result.width() <= 0 || result.height() <= 0) {
        return source;
    }
    return result;
}

bool isPreviewableSuffix(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kImageSuffixes = {
        "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "gif",
        "webp", "heic", "heif", "avif", "psd", "exr", "dpx"
    };
    static const QSet<QString> kVideoSuffixes = {
        "mov", "qt", "mp4", "m4v", "mxf", "mkv", "avi", "asf",
        "wmv", "webm", "mpg", "mpeg", "m2v", "m2ts", "mts",
        "ogv", "flv", "f4v", "3gp", "3g2", "y4m"
    };
    const QString lower = suffix.toLower();
    return kImageSuffixes.contains(lower) || kVideoSuffixes.contains(lower);
}
