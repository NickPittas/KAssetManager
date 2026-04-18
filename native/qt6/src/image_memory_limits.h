#pragma once

#include <QString>
#include <QtGlobal>

namespace ImageMemoryLimits {

constexpr qint64 kStandardDefaultLimitMb = 4096;
constexpr qint64 kHighMemoryDefaultLimitMb = 8192;
constexpr qint64 kHighMemoryThresholdMb = 64 * 1024;

qint64 detectPhysicalMemoryMb();
qint64 defaultImageMemoryLimitMb();
qint64 configuredImageMemoryLimitMb();
bool isImageWithinMemoryLimit(qint64 width, qint64 height, int channels = 4, qint64 extraMultiplier = 2);
QString limitExceededMessage(const QString& context = QString());

}
