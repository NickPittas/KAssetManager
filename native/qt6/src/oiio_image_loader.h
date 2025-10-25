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
    static QImage loadImage(const QString& filePath, int maxWidth = 0, int maxHeight = 0);
    
    /**
     * Check if a file format is supported by OIIO
     * @param filePath Path to check
     * @return true if OIIO can handle this format
     */
    static bool isOIIOSupported(const QString& filePath);
    
    /**
     * Apply simple tone mapping to HDR image data
     * @param data Float image data (RGB or RGBA)
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels (3 or 4)
     * @param exposure Exposure adjustment (default 0.0)
     * @return QImage with tone-mapped 8-bit data
     */
    static QImage toneMapHDR(const float* data, int width, int height, int channels, float exposure = 0.0f);
    
private:
    /**
     * Simple Reinhard tone mapping operator
     */
    static float reinhardToneMap(float value);
    
    /**
     * Clamp value to [0, 1] range
     */
    static float clamp(float value, float min = 0.0f, float max = 1.0f);
};

