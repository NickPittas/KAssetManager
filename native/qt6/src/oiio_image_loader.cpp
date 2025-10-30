#include "oiio_image_loader.h"
#include <QFileInfo>
#include <cmath>



#ifdef HAVE_OPENIMAGEIO
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
using namespace OIIO;
#endif

bool OIIOImageLoader::isOIIOSupported(const QString& filePath) {
#ifdef HAVE_OPENIMAGEIO
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Formats that OIIO handles better than Qt
    QStringList oiioFormats = {
        "exr", "hdr", "pic",           // HDR formats
        "psd", "psb",                  // Adobe formats
        "tif", "tiff",                 // TIFF (for 16/32-bit)
        "dpx", "cin",                  // Film formats
        "iff", "sgi", "pic", "pnm",    // Other formats
        "tga", "bmp", "ico"            // Basic formats OIIO can handle
    };

    return oiioFormats.contains(ext);
#else
    Q_UNUSED(filePath);
    return false; // OIIO not available
#endif
}

QImage OIIOImageLoader::loadImage(const QString& filePath, int maxWidth, int maxHeight, ColorSpace colorSpace) {
#ifdef HAVE_OPENIMAGEIO
    qDebug() << "[OIIOImageLoader] Loading image:" << filePath;

    // Open the image
    auto inp = ImageInput::open(filePath.toStdString());
    if (!inp) {
        qWarning() << "[OIIOImageLoader] Failed to open:" << filePath;
        qWarning() << "[OIIOImageLoader] Error:" << QString::fromStdString(OIIO::geterror());
        return QImage();
    }

    const ImageSpec &spec = inp->spec();
    int width = spec.width;
    int height = spec.height;
    int channels = spec.nchannels;

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

    // Read the image into an ImageBuf
    ImageBuf buf(filePath.toStdString());
    if (!buf.read(0, 0, true, TypeDesc::FLOAT)) {
        qWarning() << "[OIIOImageLoader] Failed to read image data";
        inp->close();
        return QImage();
    }

    inp->close();

    // Resize if needed
    if (needsResize) {
        ImageBuf resized;
        if (!ImageBufAlgo::resize(resized, buf, "", 0, ROI(0, targetWidth, 0, targetHeight))) {
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
        if (!ImageBufAlgo::channels(converted, buf, targetChannels, {}, {}, {}, true)) {
            qWarning() << "[OIIOImageLoader] Failed to convert channels";
            return QImage();
        }
        buf = std::move(converted);
    }

    // Check if this is an HDR image (float format)
    bool isHDR = (spec.format == TypeDesc::FLOAT || spec.format == TypeDesc::HALF ||
                  spec.format == TypeDesc::DOUBLE);

    if (isHDR) {
        qDebug() << "[OIIOImageLoader] HDR image detected, applying tone mapping with color space";

        // Get float data
        std::vector<float> pixels(width * height * targetChannels);
        if (!buf.get_pixels(ROI(0, width, 0, height), TypeDesc::FLOAT, pixels.data())) {
            qWarning() << "[OIIOImageLoader] Failed to get pixel data";
            return QImage();
        }

        // Apply tone mapping with color space transform
        return toneMapHDR(pixels.data(), width, height, targetChannels, colorSpace);
    } else {
        // Convert to 8-bit directly
        qDebug() << "[OIIOImageLoader] LDR image, converting to 8-bit";

        std::vector<uint8_t> pixels(width * height * targetChannels);
        if (!buf.get_pixels(ROI(0, width, 0, height), TypeDesc::UINT8, pixels.data())) {
            qWarning() << "[OIIOImageLoader] Failed to get pixel data";
            return QImage();
        }

        // Create QImage
        QImage::Format format = (targetChannels == 4) ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
        QImage image(width, height, format);

        for (int y = 0; y < height; ++y) {
            uint8_t* scanline = image.scanLine(y);
            memcpy(scanline, &pixels[y * width * targetChannels], width * targetChannels);
        }

        qDebug() << "[OIIOImageLoader] Successfully loaded image";
        return image;
    }
#else
    Q_UNUSED(filePath);
    Q_UNUSED(maxWidth);
    Q_UNUSED(maxHeight);
    qWarning() << "[OIIOImageLoader] OpenImageIO not available";
    return QImage(); // Return null image
#endif
}

QImage OIIOImageLoader::toneMapHDR(const float* data, int width, int height, int channels, ColorSpace colorSpace, float exposure) {
#ifdef HAVE_OPENIMAGEIO
    QString colorSpaceName;
    switch (colorSpace) {
        case ColorSpace::Linear: colorSpaceName = "Linear"; break;
        case ColorSpace::sRGB: colorSpaceName = "sRGB"; break;
        case ColorSpace::Rec709: colorSpaceName = "Rec.709"; break;
    }
    qDebug() << "[OIIOImageLoader] Tone mapping HDR image:" << width << "x" << height << "to" << colorSpaceName;

    QImage::Format format = (channels == 4) ? QImage::Format_RGBA8888 : QImage::Format_RGB888;
    QImage image(width, height, format);

    float exposureScale = std::pow(2.0f, exposure);

    for (int y = 0; y < height; ++y) {
        uint8_t* scanline = image.scanLine(y);

        for (int x = 0; x < width; ++x) {
            int srcIdx = (y * width + x) * channels;
            int dstIdx = x * channels;

            // Apply exposure and tone mapping to RGB channels
            for (int c = 0; c < 3; ++c) {
                float value = data[srcIdx + c] * exposureScale;

                // Apply tone mapping (Reinhard)
                value = reinhardToneMap(value);

                // Apply color space transform
                switch (colorSpace) {
                    case ColorSpace::Linear:
                        // No gamma correction, just clamp
                        value = clamp(value, 0.0f, 1.0f);
                        break;
                    case ColorSpace::sRGB:
                        // Apply sRGB gamma curve
                        value = linearToSRGB(value);
                        break;
                    case ColorSpace::Rec709:
                        // Apply Rec.709 gamma curve
                        value = linearToRec709(value);
                        break;
                }

                scanline[dstIdx + c] = uint8_t(value * 255.0f);
            }

            // Copy alpha channel if present
            if (channels == 4) {
                float alpha = clamp(data[srcIdx + 3], 0.0f, 1.0f);
                scanline[dstIdx + 3] = uint8_t(alpha * 255.0f);
            }
        }
    }

    qDebug() << "[OIIOImageLoader] Tone mapping complete";
    return image;
#else
    Q_UNUSED(data);
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(channels);
    Q_UNUSED(colorSpace);
    Q_UNUSED(exposure);
    qWarning() << "[OIIOImageLoader] OpenImageIO not available";
    return QImage(); // Return null image
#endif
}

float OIIOImageLoader::reinhardToneMap(float value) {
#ifdef HAVE_OPENIMAGEIO
    // Simple Reinhard tone mapping: x / (1 + x)
    return value / (1.0f + value);
#else
    Q_UNUSED(value);
    return 0.0f;
#endif
}

float OIIOImageLoader::linearToSRGB(float value) {
#ifdef HAVE_OPENIMAGEIO
    // sRGB gamma curve
    value = clamp(value, 0.0f, 1.0f);
    if (value <= 0.0031308f) {
        return 12.92f * value;
    } else {
        return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    }
#else
    Q_UNUSED(value);
    return 0.0f;
#endif
}

float OIIOImageLoader::linearToRec709(float value) {
#ifdef HAVE_OPENIMAGEIO
    // Rec.709 gamma curve (similar to sRGB but slightly different)
    value = clamp(value, 0.0f, 1.0f);
    if (value < 0.018f) {
        return 4.5f * value;
    } else {
        return 1.099f * std::pow(value, 0.45f) - 0.099f;
    }
#else
    Q_UNUSED(value);
    return 0.0f;
#endif
}

float OIIOImageLoader::clamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

