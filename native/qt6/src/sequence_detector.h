#pragma once
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QHash>
#include <QVector>
#include <QHashFunctions>
#include <QFileInfo>
#include <QDir>

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

    // Gap detection
    bool hasGaps = false;      // True if sequence has missing frames
    QVector<int> missingFrames; // List of missing frame numbers
    int gapCount = 0;          // Number of gaps in the sequence

    // Version tracking
    QString version;           // e.g., "v01", "v02" extracted from baseName
};

class SequenceDetector {
public:
    // Regex patterns for sequence detection (centralized for consistency)
    // Main pattern: matches "name.####.ext" or "name_####.ext" or "name####.ext"
    static const QRegularExpression& mainPattern();

    // Pattern for detecting any sequence-like filename (loose check)
    static const QRegularExpression& loosePattern();

    // Pattern for extracting the last frame number from a filename
    static const QRegularExpression& lastFramePattern();

    // Detect sequences in a list of file paths
    static QVector<ImageSequence> detectSequences(const QStringList& filePaths);

    // Check if a filename matches a sequence pattern
    static bool isSequenceFile(const QString& fileName);

    // Extract frame number from a filename
    static int extractFrameNumber(const QString& fileName, int& paddingLength);

    // Generate pattern string (e.g., "render.####.exr")
    static QString generatePattern(const QString& baseName, int paddingLength, const QString& extension);

    // Detect gaps in a sequence (missing frames)
    static void detectGaps(ImageSequence& sequence, const QVector<int>& frameNumbers);

    // Extract version string from base name (e.g., "v01", "v02")
    static QString extractVersion(const QString& baseName);

    // Build a full file path with the frame number replaced by #### (preserves original separators)
    static inline QString toHashPatternPath(const QString& filePath) {
        QFileInfo fi(filePath);
        QString name = fi.fileName();
        QRegularExpression re("(\\d{2,})(?!.*\\d)");
        QRegularExpressionMatch m = re.match(name);
        if (!m.hasMatch()) return filePath; // Not a sequence-like name
        const int pad = m.capturedLength(1);
        name.replace(m.capturedStart(1), m.capturedLength(1), QString(pad, QLatin1Char('#')));
        return fi.absoluteDir().filePath(name);
    }

    // Build a full file path with the frame number replaced by %0Nd (printf-style)
    static inline QString toPrintfPatternPath(const QString& filePath) {
        QFileInfo fi(filePath);
        QString name = fi.fileName();
        QRegularExpression re("(\\d{2,})(?!.*\\d)");
        QRegularExpressionMatch m = re.match(name);
        if (!m.hasMatch()) return filePath; // Not a sequence-like name
        const int pad = m.capturedLength(1);
        const QString fmt = QString("%%0%1d").arg(pad);
        name.replace(m.capturedStart(1), m.capturedLength(1), fmt);
        return fi.absoluteDir().filePath(name);
    }

public:
    struct SequenceKey {
        QString baseName;
        QString extension;
        int paddingLength;

        bool operator==(const SequenceKey& other) const {
            return baseName == other.baseName && extension == other.extension && paddingLength == other.paddingLength;
        }
    };

    struct FrameInfo {
        int frameNumber;
        QString filePath;
    };
};

// qHash overloads for SequenceDetector::SequenceKey
inline size_t qHash(const SequenceDetector::SequenceKey& key, size_t seed = 0) noexcept {
    return qHashMulti(seed, key.baseName, key.extension, key.paddingLength);
}
inline uint qHash(const SequenceDetector::SequenceKey& key, uint seed) noexcept {
    return qHashMulti(seed, key.baseName, key.extension, key.paddingLength);
}

