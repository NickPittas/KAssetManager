#include "oiio_image_loader.h"
#include "image_memory_limits.h"
#include <QFileInfo>
#include <stdexcept>



#if defined(HAVE_OPENIMAGEIO) && HAVE_OPENIMAGEIO
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
using namespace OIIO;
#endif

bool OIIOImageLoader::isOIIOSupported(const QString& filePath) {
#if defined(HAVE_OPENIMAGEIO) && HAVE_OPENIMAGEIO
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Use OIIO for all formats it supports to ensure consistent behavior
    // This eliminates redundancy with Qt's image reader and provides better quality
    QStringList oiioFormats = {
        // HDR formats (require tone mapping)
        "exr", "hdr", "pfm", "pic",
        // Adobe formats
        "psd", "psb",
        // TIFF (for 16/32-bit support)
        "tif", "tiff",
        // Film/professional formats
        "dpx", "cin",
        // Other professional formats
        "iff", "sgi", "pnm",
        // Common formats (OIIO provides consistent aspect ratio handling and color management)
        "jpg", "jpeg", "png", "bmp", "tga", "ico", "gif"
    };

    return oiioFormats.contains(ext);
#else
    Q_UNUSED(filePath);
    return false; // OIIO not available
#endif
}

QImage OIIOImageLoader::loadImage(const QString& filePath, int maxWidth, int maxHeight) {
#if defined(HAVE_OPENIMAGEIO) && HAVE_OPENIMAGEIO
    // Wrap entire OIIO operation in try-catch to prevent crashes from corrupted files
    // or OIIO internal errors from propagating and terminating the application
    try {
        qDebug() << "[OIIOImageLoader] Loading image:" << filePath;
        qDebug() << "[OIIOImageLoader] Target size:" << maxWidth << "x" << maxHeight;

        // Validate file exists before attempting to load
        if (filePath.isEmpty()) {
            qWarning() << "[OIIOImageLoader] Empty file path";
            return QImage();
        }

        // Use ImageBuf to manage the input resource (RAII) and avoid manual ImageInput handling
        ImageBuf buf(filePath.toStdString());
        if (!buf.read(0, 0, true, TypeDesc::FLOAT)) {
            QString errorMsg = QString::fromStdString(buf.geterror());
            qWarning() << "[OIIOImageLoader] Failed to read image data:" << errorMsg;
            qWarning() << "[OIIOImageLoader] File:" << filePath;
            return QImage();
        }

        const ImageSpec &spec = buf.spec();
        int width = spec.width;
        int height = spec.height;
        int channels = spec.nchannels;

        // Validate dimensions to prevent crashes from corrupted files
        if (width <= 0 || height <= 0 || channels <= 0) {
            qWarning() << "[OIIOImageLoader] Invalid image dimensions:" << width << "x" << height << "channels:" << channels;
            return QImage();
        }

        if (!ImageMemoryLimits::isImageWithinMemoryLimit(width, height, qMax(channels, 4), 2)) {
            qWarning() << "[OIIOImageLoader] Image exceeds configured memory limit:" << width << "x" << height;
            return QImage();
        }

        qDebug() << "[OIIOImageLoader] Image info:" << width << "x" << height << "channels:" << channels;
        qDebug() << "[OIIOImageLoader] Format:" << QString::fromStdString(spec.format.c_str());

        // Check if we need to resize for thumbnail
        bool needsResize = false;
        int targetWidth = width;
        int targetHeight = height;

        if (maxWidth > 0 && maxHeight > 0) {
        if (width > maxWidth || height > maxHeight) {
            needsResize = true;
            float scale = std::min(float(maxWidth) / width, float(maxHeight) / height);
            targetWidth = int(width * scale);
            targetHeight = int(height * scale);
            qDebug() << "[OIIOImageLoader] Will resize to:" << targetWidth << "x" << targetHeight;
        }
    }

    // Resize if needed
    if (needsResize) {
        ImageBuf resized;
        if (!ImageBufAlgo::resize(resized, buf, ImageBufAlgo::KWArgs{}, ROI(0, targetWidth, 0, targetHeight))) {
            qWarning() << "[OIIOImageLoader] Failed to resize image";
            return QImage();
        }
        buf = std::move(resized);
        width = targetWidth;
        height = targetHeight;
    }

    // Convert to RGB or RGBA
    int targetChannels = (channels >= 4) ? 4 : 3;
    ImageBuf converted;
    if (channels != targetChannels) {
        // For grayscale images (1 channel), replicate to RGB by specifying channel order
        if (channels == 1 && targetChannels == 3) {
            // Replicate channel 0 to R, G, and B
            std::vector<int> channelOrder = {0, 0, 0};
            if (!ImageBufAlgo::channels(converted, buf, targetChannels, channelOrder, {}, {}, true)) {
                qWarning() << "[OIIOImageLoader] Failed to convert grayscale to RGB";
                return QImage();
            }
        } else {
            // Standard channel conversion
            if (!ImageBufAlgo::channels(converted, buf, targetChannels, {}, {}, {}, true)) {
                qWarning() << "[OIIOImageLoader] Failed to convert channels";
                return QImage();
            }
        }
        buf = std::move(converted);
    }

        std::vector<uint8_t> pixels(width * height * targetChannels);
        if (!buf.get_pixels(ROI(0, width, 0, height), TypeDesc::UINT8, pixels.data())) {
            QString errorMsg = QString::fromStdString(buf.geterror());
            qWarning() << "[OIIOImageLoader] Failed to get image pixel data:" << errorMsg;
            return QImage();
        }

        QImage::Format format = (targetChannels == 4) ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        QImage image(width, height, format);

        for (int y = 0; y < height; ++y) {
            uint8_t* scanline = image.scanLine(y);
            memcpy(scanline, &pixels[y * width * targetChannels], width * targetChannels);
        }

        qDebug() << "[OIIOImageLoader] Successfully loaded image";
        return image;
    } catch (const std::exception& e) {
        qWarning() << "[OIIOImageLoader] Exception loading image" << filePath << ":" << e.what();
        return QImage();
    } catch (...) {
        qWarning() << "[OIIOImageLoader] Unknown exception loading image" << filePath;
        return QImage();
    }
#else
    Q_UNUSED(filePath);
    Q_UNUSED(maxWidth);
    Q_UNUSED(maxHeight);
    qWarning() << "[OIIOImageLoader] OpenImageIO not available";
    return QImage(); // Return null image
#endif
}
