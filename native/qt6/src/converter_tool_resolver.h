#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

enum class ConverterToolSource {
    BundledRuntime,
    BundledDevCheckout,
    EnvOverride,
    PathFallback,
    Missing
};

struct ConverterToolResolution {
    QString toolName;
    QString resolvedPath;
    ConverterToolSource source = ConverterToolSource::Missing;

    bool isAbsolute() const { return QFileInfo(resolvedPath).isAbsolute(); }
    bool isFallback() const { return source == ConverterToolSource::PathFallback; }
};

namespace ConverterToolResolverDetail {

inline QString executableNameFor(const QString& toolName)
{
#ifdef Q_OS_WIN
    return toolName + QStringLiteral(".exe");
#else
    return toolName;
#endif
}

inline QString ffprobeExecutableName()
{
    return executableNameFor(QStringLiteral("ffprobe"));
}

inline ConverterToolResolution resolutionFor(const QString& toolName,
                                             const QString& resolvedPath,
                                             ConverterToolSource source)
{
    return ConverterToolResolution{toolName, resolvedPath, source};
}

inline ConverterToolResolution findFirstExisting(const QString& toolName,
                                                 const QStringList& candidates,
                                                 ConverterToolSource source)
{
    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile()) {
            return resolutionFor(toolName, info.absoluteFilePath(), source);
        }
    }
    return resolutionFor(toolName, QString(), ConverterToolSource::Missing);
}

inline QStringList runtimeCandidates(const QString& appDir, const QString& executableName)
{
    return {
        QDir(appDir).filePath(executableName),
        QDir(appDir).filePath(QStringLiteral("../bin/%1").arg(executableName))
    };
}

inline QStringList envCandidates(const QString& envRoot, const QString& executableName)
{
    if (envRoot.isEmpty()) {
        return {};
    }

    return {
        QDir(envRoot).filePath(executableName),
        QDir(envRoot).filePath(QStringLiteral("bin/%1").arg(executableName))
    };
}

inline ConverterToolResolution resolveFfmpegToolImpl(const QString& appDir)
{
    const QString toolName = QStringLiteral("ffmpeg");
    const QString executableName = executableNameFor(toolName);

    if (const auto runtime = findFirstExisting(toolName, runtimeCandidates(appDir, executableName), ConverterToolSource::BundledRuntime);
        runtime.source != ConverterToolSource::Missing) {
        return runtime;
    }

    const QString thirdPartyPath = QDir(appDir).filePath(QStringLiteral("../../third_party/ffmpeg/bin/%1").arg(executableName));
    if (const auto dev = findFirstExisting(toolName, {thirdPartyPath}, ConverterToolSource::BundledDevCheckout);
        dev.source != ConverterToolSource::Missing) {
        return dev;
    }

    const QString envRoot = qEnvironmentVariable("FFMPEG_ROOT");
    if (const auto env = findFirstExisting(toolName, envCandidates(envRoot, executableName), ConverterToolSource::EnvOverride);
        env.source != ConverterToolSource::Missing) {
        return env;
    }

    return resolutionFor(toolName, toolName, ConverterToolSource::PathFallback);
}

inline ConverterToolResolution resolveMagickToolImpl(const QString& appDir)
{
    const QString toolName = QStringLiteral("magick");
    const QString executableName = executableNameFor(toolName);

    if (const auto runtime = findFirstExisting(toolName, runtimeCandidates(appDir, executableName), ConverterToolSource::BundledRuntime);
        runtime.source != ConverterToolSource::Missing) {
        return runtime;
    }

    const QString thirdPartyRoot = QDir(appDir).filePath(QStringLiteral("../../third_party"));
    const QDir thirdPartyDir(thirdPartyRoot);
    if (thirdPartyDir.exists()) {
        if (const auto lowerCase = findFirstExisting(toolName,
                                                     {QDir(thirdPartyRoot).filePath(QStringLiteral("imagemagick/bin/%1").arg(executableName))},
                                                     ConverterToolSource::BundledDevCheckout);
            lowerCase.source != ConverterToolSource::Missing) {
            return lowerCase;
        }

        const QStringList imageMagickDirs = thirdPartyDir.entryList(QStringList() << QStringLiteral("ImageMagick*"),
                                                                    QDir::Dirs | QDir::NoDotAndDotDot,
                                                                    QDir::Name);
        for (const QString& dirName : imageMagickDirs) {
            const QString root = thirdPartyDir.filePath(dirName);
            if (const auto dev = findFirstExisting(toolName,
                                                   {QDir(root).filePath(executableName),
                                                    QDir(root).filePath(QStringLiteral("bin/%1").arg(executableName))},
                                                   ConverterToolSource::BundledDevCheckout);
                dev.source != ConverterToolSource::Missing) {
                return dev;
            }
        }
    }

    const QStringList envVars = {QStringLiteral("MAGICK_ROOT"), QStringLiteral("IMAGEMAGICK_ROOT")};
    for (const QString& envVar : envVars) {
        if (const auto env = findFirstExisting(toolName,
                                               envCandidates(qEnvironmentVariable(envVar.toUtf8().constData()), executableName),
                                               ConverterToolSource::EnvOverride);
            env.source != ConverterToolSource::Missing) {
            return env;
        }
    }

    return resolutionFor(toolName, toolName, ConverterToolSource::PathFallback);
}

} // namespace ConverterToolResolverDetail

inline ConverterToolResolution resolveFfmpegTool(const QString& appDir)
{
    return ConverterToolResolverDetail::resolveFfmpegToolImpl(appDir);
}

inline ConverterToolResolution resolveMagickTool(const QString& appDir)
{
    return ConverterToolResolverDetail::resolveMagickToolImpl(appDir);
}

inline QString resolveSiblingFfprobePath(const QString& ffmpegPath)
{
    if (!ffmpegPath.isEmpty()) {
        const QFileInfo ffmpegInfo(ffmpegPath);
        if (ffmpegInfo.isAbsolute()) {
            const QString sibling = ffmpegInfo.dir().filePath(ConverterToolResolverDetail::ffprobeExecutableName());
            if (QFileInfo::exists(sibling)) {
                return QFileInfo(sibling).absoluteFilePath();
            }
        }
    }

    return QStringLiteral("ffprobe");
}
