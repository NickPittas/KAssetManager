#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

namespace RealMediaTestHelper {

struct ResolvedMediaFile {
    QString filePath;
    QString skipReason;

    [[nodiscard]] bool isAvailable() const
    {
        return !filePath.isEmpty();
    }
};

inline QString realMediaDir()
{
    return qEnvironmentVariable("KASSETMANAGER_REAL_MEDIA_DIR").trimmed();
}

inline ResolvedMediaFile resolve(const QString& fileName, const char* skipMessagePrefix = "Real media file unavailable")
{
    const QString mediaDir = realMediaDir();
    if (mediaDir.isEmpty()) {
        return {{}, QStringLiteral("Set KASSETMANAGER_REAL_MEDIA_DIR to run real-media tests")};
    }

    const QString filePath = QDir(mediaDir).filePath(fileName);
    if (!QFileInfo::exists(filePath)) {
        return {{}, QStringLiteral("%1: %2").arg(QString::fromUtf8(skipMessagePrefix), filePath)};
    }

    return {QFileInfo(filePath).absoluteFilePath(), {}};
}

} // namespace RealMediaTestHelper
