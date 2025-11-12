#include "live_preview_manager.h"

void LivePreviewManager::onFFmpegFrameReady(const FFmpegPlayer::VideoFrame& frame)
{
    // Convert FFmpegPlayer::VideoFrame to LivePreviewManager format
    if (!frame.isValid()) {
        return;
    }
    
    QPixmap pixmap = QPixmap::fromImage(frame.image);
    if (!pixmap.isNull()) {
        // Convert position from timestamp to normalized [0,1] range
        qreal normalizedPos = frame.timestampMs > 0 ? 
            static_cast<qreal>(frame.timestampMs) / 1000.0 / (frame.fps > 0 ? frame.fps : 25.0) : 0.0;
        
        // Cache the frame for future requests
        QString cacheKey = makeCacheKey(m_currentFilePath, QSize(frame.width, frame.height), normalizedPos);
        storeFrame(cacheKey, pixmap, normalizedPos, QSize(frame.width, frame.height));
        
        // Emit in the expected format for LivePreviewManager
        emit frameReady(m_currentFilePath, normalizedPos, QSize(frame.width, frame.height), pixmap);
    }
}

void LivePreviewManager::onFFmpegError(const QString& errorString)
{
    emit frameFailed(m_currentFilePath, errorString);
}