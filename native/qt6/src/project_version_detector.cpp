#include "project_version_detector.h"
#include <QFileInfo>
#include <QMap>
#include <algorithm>

// Static regex initialization
// Matches: _v001, -v01, .v1, _V001, -V01 etc. (case insensitive)
QRegularExpression ProjectVersionDetector::s_versionWithVRegex(
    R"([-_.\s]v(\d{1,4})$)",
    QRegularExpression::CaseInsensitiveOption
);

// Matches: _001, -01, _1 etc. (numeric version without 'v' prefix, at end of basename)
QRegularExpression ProjectVersionDetector::s_versionNumericRegex(
    R"([-_](\d{1,4})$)"
);

bool ProjectVersionDetector::isProjectFile(const QString& filePath) {
    return isAfterEffectsFile(filePath) || isNukeFile(filePath);
}

bool ProjectVersionDetector::isAfterEffectsFile(const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    return ext == "aep" || ext == "aepx";
}

bool ProjectVersionDetector::isNukeFile(const QString& filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();
    return ext == "nk";
}

VersionInfo ProjectVersionDetector::parseVersion(const QString& filePath) {
    VersionInfo info;
    info.fullPath = filePath;
    
    QFileInfo fi(filePath);
    info.extension = fi.suffix().toLower();
    
    // Only process supported project files
    if (!isProjectFile(filePath)) {
        info.baseName = fi.completeBaseName();
        info.versionGroupKey = info.baseName + "." + info.extension;
        return info;
    }
    
    QString baseName = fi.completeBaseName();
    
    // Try pattern with 'v' prefix first (more specific)
    QRegularExpressionMatch matchV = s_versionWithVRegex.match(baseName);
    if (matchV.hasMatch()) {
        info.versionString = matchV.captured(0).mid(1); // Remove leading separator
        info.versionNumber = matchV.captured(1).toInt();
        info.baseName = baseName.left(matchV.capturedStart());
        info.versionGroupKey = info.baseName + "." + info.extension;
        return info;
    }
    
    // Try numeric-only pattern (After Effects style: filename_001.aep)
    QRegularExpressionMatch matchNum = s_versionNumericRegex.match(baseName);
    if (matchNum.hasMatch()) {
        info.versionString = matchNum.captured(1);
        info.versionNumber = matchNum.captured(1).toInt();
        info.baseName = baseName.left(matchNum.capturedStart());
        info.versionGroupKey = info.baseName + "." + info.extension;
        return info;
    }
    
    // No version detected - use full basename as key
    info.baseName = baseName;
    info.versionGroupKey = baseName + "." + info.extension;
    return info;
}

QMap<QString, QVector<QPair<int, QString>>> ProjectVersionDetector::groupByVersion(const QStringList& filePaths) {
    QMap<QString, QVector<QPair<int, QString>>> groups;
    
    for (const QString& path : filePaths) {
        if (!isProjectFile(path)) continue;
        
        VersionInfo info = parseVersion(path);
        
        // Only group if version was detected (versionNumber > 0)
        // Files without versions get their own "group" of size 1
        groups[info.versionGroupKey].append({info.versionNumber, path});
    }
    
    // Sort each group by version number descending (highest first)
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
            return a.first > b.first; // Descending order
        });
    }
    
    return groups;
}

QString ProjectVersionDetector::getHighestVersionPath(const QVector<QPair<int, QString>>& versions) {
    if (versions.isEmpty()) return QString();
    
    // First element is highest due to descending sort
    return versions.first().second;
}
