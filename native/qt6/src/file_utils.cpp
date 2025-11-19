#include "file_utils.h"
#include <QSet>

namespace FileUtils {

bool isPreviewableSuffix(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kImageSuffixes = {
        "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "gif",
        "webp", "heic", "heif", "avif", "psd", "exr", "hdr", "pfm", "dpx"
    };
    static const QSet<QString> kVideoSuffixes = {
        "mov", "qt", "mp4", "m4v", "mxf", "mkv", "avi", "asf",
        "wmv", "webm", "mpg", "mpeg", "m2v", "m2ts", "mts",
        "ogv", "flv", "f4v", "3gp", "3g2", "y4m"
    };
    const QString lower = suffix.toLower();
    return kImageSuffixes.contains(lower) || kVideoSuffixes.contains(lower);
}

} // namespace FileUtils
