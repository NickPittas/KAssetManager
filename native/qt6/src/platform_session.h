#pragma once

#include <QString>

namespace PlatformSession {

inline bool isWayland(const QString &platformName, const QString &sessionType)
{
#if defined(Q_OS_LINUX)
    return platformName.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive) ||
           sessionType.compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0;
#else
    Q_UNUSED(platformName);
    Q_UNUSED(sessionType);
    return false;
#endif
}

inline bool isWayland()
{
#if defined(Q_OS_LINUX)
    return isWayland(qEnvironmentVariable("QT_QPA_PLATFORM"), qEnvironmentVariable("XDG_SESSION_TYPE"));
#else
    return false;
#endif
}

inline bool shouldForceRasterWidgetsOnWayland()
{
    return false;
}

inline bool shouldUseDesktopOpenGLOnWayland()
{
    return false;
}

inline bool shouldUseRasterPreviewFallbackOnWayland()
{
    return isWayland();
}

} // namespace PlatformSession
