#include "runtime_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDevice>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace {

bool isAppImageRuntime()
{
    return qEnvironmentVariableIsSet("APPDIR") || qEnvironmentVariableIsSet("APPIMAGE");
}

bool isDirectoryWritableOrCreatable(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists()) {
        return info.isDir() && info.isWritable();
    }

    const QFileInfo parentInfo(QFileInfo(path).dir().absolutePath());
    return parentInfo.exists() && parentInfo.isDir() && parentInfo.isWritable();
}

QString ensureDirectory(const QString& path)
{
    QDir().mkpath(path);
    return path;
}

bool copyFileIfMissing(const QString& sourcePath, const QString& targetPath)
{
    if (QFile::exists(targetPath)) {
        return true;
    }

    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    return QFile::copy(sourcePath, targetPath);
}

void copyDirectoryContentsIfMissing(const QString& sourceDirPath, const QString& targetDirPath)
{
    const QDir sourceDir(sourceDirPath);
    if (!sourceDir.exists()) {
        return;
    }

    QDir().mkpath(targetDirPath);

    const QFileInfoList entries = sourceDir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (const QFileInfo& entry : entries) {
        const QString sourcePath = entry.absoluteFilePath();
        const QString targetPath = QDir(targetDirPath).filePath(entry.fileName());
        if (entry.isDir()) {
            copyDirectoryContentsIfMissing(sourcePath, targetPath);
        } else {
            copyFileIfMissing(sourcePath, targetPath);
        }
    }
}

} // namespace

namespace RuntimePaths {

QString portableDataRoot()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

QString userDataRoot()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    return path;
}

bool usingPortableDataRoot()
{
    if (isAppImageRuntime()) {
        return false;
    }
    return isDirectoryWritableOrCreatable(portableDataRoot());
}

QString writableDataRoot()
{
    if (usingPortableDataRoot()) {
        return ensureDirectory(portableDataRoot());
    }

    const QString perUserRoot = userDataRoot();
    if (!perUserRoot.isEmpty()) {
        return ensureDirectory(perUserRoot);
    }

    return ensureDirectory(portableDataRoot());
}

QString dataPath(const QString& relativePath)
{
    const QString root = writableDataRoot();
    if (relativePath.isEmpty()) {
        return root;
    }
    return QDir(root).filePath(relativePath);
}

void migrateLegacyDataToWritableRoot()
{
    const QString legacyRoot = portableDataRoot();
    const QString targetRoot = writableDataRoot();
    if (QDir::cleanPath(legacyRoot) == QDir::cleanPath(targetRoot)) {
        return;
    }

    copyDirectoryContentsIfMissing(legacyRoot, targetRoot);
}

} // namespace RuntimePaths
