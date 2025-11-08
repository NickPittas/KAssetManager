#include "ui/file_type_helpers.h"

namespace {

inline QString normalize(const QString& ext)
{
    return ext.toLower();
}

} // namespace

bool isImageFile(const QString& ext)
{
    static const QSet<QString> exts = {
        "png","jpg","jpeg","bmp","gif","tif","tiff","webp","heic",
        "heif","exr","psd","dpx","tga","avif"
    };
    return exts.contains(normalize(ext));
}

bool isVideoFile(const QString& ext)
{
    static const QSet<QString> exts = {
        "mp4","mov","avi","mkv","wmv","m4v","mpg","mpeg","mxf",
        "webm","qt","asf","m2v","m2ts","mts","ogv","flv","f4v",
        "3gp","3g2","y4m"
    };
    return exts.contains(normalize(ext));
}

bool isAudioFile(const QString& ext)
{
    static const QSet<QString> exts = {"mp3","wav","aac","flac","ogg","m4a"};
    return exts.contains(normalize(ext));
}

bool isPdfFile(const QString& ext)
{
    return normalize(ext) == "pdf";
}

bool isSvgFile(const QString& ext)
{
    static const QSet<QString> exts = {"svg","svgz"};
    return exts.contains(normalize(ext));
}

bool isTextFile(const QString& ext)
{
    static const QSet<QString> exts = {"txt","log","md"};
    return exts.contains(normalize(ext));
}

bool isCsvFile(const QString& ext)
{
    return normalize(ext) == "csv";
}

bool isExcelFile(const QString& ext)
{
    static const QSet<QString> exts = {"xls","xlsx"};
    return exts.contains(normalize(ext));
}

bool isDocxFile(const QString& ext)
{
    return normalize(ext) == "docx";
}

bool isDocFile(const QString& ext)
{
    return normalize(ext) == "doc";
}

bool isAiFile(const QString& ext)
{
    return normalize(ext) == "ai";
}

bool isPptxFile(const QString& ext)
{
    const QString lower = normalize(ext);
    return lower == "pptx" || lower == "ppt";
}
