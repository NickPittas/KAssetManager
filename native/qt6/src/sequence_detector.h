#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QMap>
#include <QVector>

struct ImageSequence {
    QString pattern;           // e.g., "render.####.exr"
    QString baseName;          // e.g., "render"
    QString extension;         // e.g., "exr"
    int paddingLength;         // e.g., 4 for ####
    int startFrame;
    int endFrame;
    int frameCount;
    QStringList framePaths;    // All file paths in the sequence
    QString firstFramePath;    // Path to first frame (for thumbnail)
};

class SequenceDetector {
public:
    // Detect sequences in a list of file paths
    static QVector<ImageSequence> detectSequences(const QStringList& filePaths);
    
    // Check if a filename matches a sequence pattern
    static bool isSequenceFile(const QString& fileName);
    
    // Extract frame number from a filename
    static int extractFrameNumber(const QString& fileName, int& paddingLength);
    
    // Generate pattern string (e.g., "render.####.exr")
    static QString generatePattern(const QString& baseName, int paddingLength, const QString& extension);
    
private:
    struct SequenceKey {
        QString baseName;
        QString extension;
        int paddingLength;
        
        bool operator<(const SequenceKey& other) const {
            if (baseName != other.baseName) return baseName < other.baseName;
            if (extension != other.extension) return extension < other.extension;
            return paddingLength < other.paddingLength;
        }
    };
    
    struct FrameInfo {
        int frameNumber;
        QString filePath;
    };
};

