#pragma once
#include <QString>
#include <QImage>
#include <QDebug>

/**
 * OpenImageIO-based image loader for advanced formats
 * Supports: EXR, HDR, PSD, RAW, TIFF (16/32-bit), and more
 */
class OIIOImageLoader {
public:
    /**
     * Load an image using OpenImageIO
     * @param filePath Path to the image file
     * @param maxWidth Maximum width for the loaded image (for thumbnails)
     * @param maxHeight Maximum height for the loaded image (for thumbnails)
     * @return QImage containing the loaded image, or null QImage on failure
     */
    static QImage loadImage(const QString& filePath,
                            int maxWidth = 0,
                            int maxHeight = 0);

    /**
     * Check if a file format is supported by OIIO
     * @param filePath Path to check
     * @return true if OIIO can handle this format
     */
    static bool isOIIOSupported(const QString& filePath);

};
