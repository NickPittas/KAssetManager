#include "image_memory_limits.h"

#include <QSettings>
#include <QString>

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/sysinfo.h>
#elif defined(Q_OS_MAC)
#include <sys/sysctl.h>
#endif

namespace {
constexpr auto kImageMemoryLimitSetting = "Images/MemoryLimitMB";

qint64 clampImageMemoryLimitMb(qint64 value)
{
    return qBound<qint64>(512LL, value, 262144LL);
}
}

namespace ImageMemoryLimits {

qint64 detectPhysicalMemoryMb()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        return static_cast<qint64>(memInfo.ullTotalPhys / (1024 * 1024));
    }
#elif defined(Q_OS_LINUX)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        const qint64 unit = info.mem_unit > 0 ? static_cast<qint64>(info.mem_unit) : 1;
        const qint64 totalBytes = static_cast<qint64>(info.totalram) * unit;
        if (totalBytes > 0) {
            return totalBytes / (1024 * 1024);
        }
    }
#elif defined(Q_OS_MAC)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize = 0;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, nullptr, 0) == 0) {
        return static_cast<qint64>(memsize / (1024 * 1024));
    }
#endif
    return 8192;
}

qint64 defaultImageMemoryLimitMb()
{
    return detectPhysicalMemoryMb() > kHighMemoryThresholdMb ? kHighMemoryDefaultLimitMb
                                                             : kStandardDefaultLimitMb;
}

qint64 configuredImageMemoryLimitMb()
{
    QSettings settings("AugmentCode", "KAssetManager");
    return clampImageMemoryLimitMb(settings.value(kImageMemoryLimitSetting, defaultImageMemoryLimitMb()).toLongLong());
}

bool isImageWithinMemoryLimit(qint64 width, qint64 height, int channels, qint64 extraMultiplier)
{
    if (width <= 0 || height <= 0 || channels <= 0 || extraMultiplier <= 0) {
        return false;
    }

    const qint64 bytesPerPixel = channels * static_cast<qint64>(sizeof(float));
    const qint64 estimatedBytes = width * height * bytesPerPixel * extraMultiplier;
    const qint64 limitBytes = configuredImageMemoryLimitMb() * 1024LL * 1024LL;
    return estimatedBytes <= limitBytes;
}

QString limitExceededMessage(const QString& context)
{
    const QString prefix = context.isEmpty() ? QStringLiteral("Image exceeds the configured memory limit")
                                             : context;
    return QStringLiteral("%1 (%2 MB). Increase Settings > Cache & Database > Image memory limit if needed.")
        .arg(prefix)
        .arg(configuredImageMemoryLimitMb());
}

}
