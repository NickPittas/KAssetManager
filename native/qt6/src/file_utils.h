#pragma once

#include <QString>
#include <QFileInfo>
#include <QFile>

/**
 * FileUtils - Standardized file operations utilities
 *
 * Provides consistent, centralized file existence validation and related operations.
 * All external file path operations should use these helpers to ensure consistent
 * error handling and logging.
 */
namespace FileUtils {

/**
 * Check if a file exists at the given path.
 * Standardized across the codebase to use QFileInfo consistently.
 *
 * @param filePath The file path to check
 * @return true if the file exists and is a regular file, false otherwise
 */
inline bool fileExists(const QString& filePath)
{
    QFileInfo fi(filePath);
    return fi.exists() && fi.isFile();
}

/**
 * Check if a directory exists at the given path.
 *
 * @param dirPath The directory path to check
 * @return true if the directory exists, false otherwise
 */
inline bool dirExists(const QString& dirPath)
{
    QFileInfo fi(dirPath);
    return fi.exists() && fi.isDir();
}

/**
 * Check if a path exists (file or directory).
 *
 * @param path The path to check
 * @return true if the path exists, false otherwise
 */
inline bool pathExists(const QString& path)
{
    return QFileInfo::exists(path);
}

} // namespace FileUtils

