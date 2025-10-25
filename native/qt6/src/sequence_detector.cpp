#include "sequence_detector.h"
#include <QFileInfo>
#include <QDebug>
#include <QMap>

QVector<ImageSequence> SequenceDetector::detectSequences(const QStringList& filePaths) {
    QMap<SequenceKey, QVector<FrameInfo>> sequenceGroups;
    QStringList nonSequenceFiles;
    
    // Group files by sequence pattern
    for (const QString& filePath : filePaths) {
        QFileInfo fi(filePath);
        QString fileName = fi.fileName();
        
        int paddingLength = 0;
        int frameNumber = extractFrameNumber(fileName, paddingLength);
        
        if (frameNumber >= 0 && paddingLength > 0) {
            // This is a sequence file
            QString baseName = fileName;
            QString extension = fi.suffix();
            
            // Remove the frame number and extension to get base name
            // Pattern: name.####.ext or name_####.ext or name####.ext
            QRegularExpression re(QString("(\\d{%1})").arg(paddingLength));
            baseName.remove(re);
            baseName.remove("." + extension);
            baseName.remove("_" + extension);
            
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
    QRegularExpression re("(\\d{3,})");
    QRegularExpressionMatch match = re.match(fileName);
    
    if (!match.hasMatch()) {
        paddingLength = 0;
        return -1;
    }
    
    QString numberStr = match.captured(1);
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

