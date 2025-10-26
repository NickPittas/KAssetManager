#include "sequence_detector.h"
#include <QFileInfo>
#include <QDebug>
#include <QMap>

QVector<ImageSequence> SequenceDetector::detectSequences(const QStringList& filePaths) {
    QMap<SequenceKey, QVector<FrameInfo>> sequenceGroups;
    QStringList nonSequenceFiles;

    // Define image extensions (not video)
    QStringList imageExtensions = {
        "jpg", "jpeg", "png", "gif", "bmp", "tif", "tiff", "webp", "svg",
        "exr", "hdr", "pic", "psd", "psb", "dpx", "cin", "iff", "sgi",
        "tga", "ico", "pbm", "pgm", "ppm", "pnm",
        "cr2", "cr3", "nef", "arw", "dng", "orf", "rw2", "pef", "srw", "raf", "raw"
    };

    // Group files by sequence pattern
    for (const QString& filePath : filePaths) {
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();
        QString extension = fi.suffix().toLower();

        // Only detect sequences for image files, not videos
        if (!imageExtensions.contains(extension)) {
            nonSequenceFiles.append(filePath);
            continue;
        }

        int paddingLength = 0;
        int frameNumber = extractFrameNumber(fileName, paddingLength);

        if (frameNumber >= 0 && paddingLength > 0) {
            // This is a sequence file
            // Find the LAST occurrence of the frame number pattern
            QRegularExpression re(QString("(\\d{%1})").arg(paddingLength));
            QRegularExpressionMatchIterator it = re.globalMatch(fileName);

            QRegularExpressionMatch lastMatch;
            while (it.hasNext()) {
                lastMatch = it.next();
            }

            // Remove only the last occurrence (the frame number) and extension
            QString baseName = fileName;
            if (lastMatch.hasMatch()) {
                int matchStart = lastMatch.capturedStart(1);
                int matchLength = lastMatch.capturedLength(1);
                baseName.remove(matchStart, matchLength);
            }

            // Remove extension
            if (baseName.endsWith("." + extension)) {
                baseName.chop(extension.length() + 1);
            }

            // Clean up any trailing dots or underscores
            while (baseName.endsWith('.') || baseName.endsWith('_')) {
                baseName.chop(1);
            }

            SequenceKey key;
            key.baseName = baseName;
            key.extension = extension;
            key.paddingLength = paddingLength;

            FrameInfo frame;
            frame.frameNumber = frameNumber;
            frame.filePath = filePath;

            sequenceGroups[key].append(frame);
        } else {
            nonSequenceFiles.append(filePath);
        }
    }
    
    // Build sequence objects
    QVector<ImageSequence> sequences;
    
    for (auto it = sequenceGroups.begin(); it != sequenceGroups.end(); ++it) {
        const SequenceKey& key = it.key();
        QVector<FrameInfo>& frames = it.value();
        
        // Only treat as sequence if we have 2+ frames
        if (frames.size() < 2) {
            // Single frame, treat as regular file
            for (const FrameInfo& frame : frames) {
                nonSequenceFiles.append(frame.filePath);
            }
            continue;
        }
        
        // Sort frames by frame number
        std::sort(frames.begin(), frames.end(), [](const FrameInfo& a, const FrameInfo& b) {
            return a.frameNumber < b.frameNumber;
        });
        
        ImageSequence seq;
        seq.baseName = key.baseName;
        seq.extension = key.extension;
        seq.paddingLength = key.paddingLength;
        seq.startFrame = frames.first().frameNumber;
        seq.endFrame = frames.last().frameNumber;
        seq.frameCount = frames.size();
        seq.firstFramePath = frames.first().filePath;
        
        // Generate pattern
        seq.pattern = generatePattern(key.baseName, key.paddingLength, key.extension);
        
        // Store all frame paths
        for (const FrameInfo& frame : frames) {
            seq.framePaths.append(frame.filePath);
        }
        
        sequences.append(seq);
        
        qDebug() << "[SequenceDetector] Detected sequence:" << seq.pattern 
                 << "frames:" << seq.startFrame << "-" << seq.endFrame 
                 << "count:" << seq.frameCount;
    }
    
    return sequences;
}

bool SequenceDetector::isSequenceFile(const QString& fileName) {
    // Check for common sequence patterns:
    // name.####.ext
    // name_####.ext
    // name####.ext
    
    QRegularExpression re("\\d{3,}"); // 3 or more consecutive digits
    return re.match(fileName).hasMatch();
}

int SequenceDetector::extractFrameNumber(const QString& fileName, int& paddingLength) {
    // Find sequences of digits (3+ digits for padding detection)
    // Use globalMatch to find ALL occurrences, then take the LAST one
    // This handles cases like "C0642_comp_v01.1001.exr" where we want 1001, not 0642
    QRegularExpression re("(\\d{3,})");
    QRegularExpressionMatchIterator it = re.globalMatch(fileName);

    QRegularExpressionMatch lastMatch;
    bool hasMatch = false;

    // Find the last match
    while (it.hasNext()) {
        lastMatch = it.next();
        hasMatch = true;
    }

    if (!hasMatch) {
        paddingLength = 0;
        return -1;
    }

    QString numberStr = lastMatch.captured(1);
    paddingLength = numberStr.length();

    bool ok;
    int frameNumber = numberStr.toInt(&ok);

    if (!ok) {
        paddingLength = 0;
        return -1;
    }

    return frameNumber;
}

QString SequenceDetector::generatePattern(const QString& baseName, int paddingLength, const QString& extension) {
    QString padding;
    for (int i = 0; i < paddingLength; ++i) {
        padding += "#";
    }
    
    // Determine separator (dot or underscore)
    // Default to dot
    return QString("%1.%2.%3").arg(baseName).arg(padding).arg(extension);
}

