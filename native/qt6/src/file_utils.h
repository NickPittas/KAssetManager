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

/**
 * Path availability checking mode.
 */
enum class PathAvailabilityMode {
    DirectoryOnly,          ///< path must itself be an existing readable directory
    FileOrContainingDirectory ///< if path points to a file, validate its parent directory
};

/**
 * Reason a path failed availability checks.
 */
enum class PathAvailabilityFailure {
    None,
    EmptyPath,
    Missing,
    NotDirectory,
    NotReadable,
    NotSearchable,
    StorageInvalid,
    StorageNotReady,
    StorageFingerprintMismatch
};

/**
 * Result of a path availability check.
 */
struct PathAvailabilityResult {
    bool available = false;
    PathAvailabilityFailure failure = PathAvailabilityFailure::None;
    QString normalizedPath;      ///< canonical directory used for navigation
    QString directoryPath;       ///< same as normalizedPath (the directory being validated)
    QString storageFingerprint;  ///< stable fingerprint of the backing storage
    QString message;             ///< user/log suitable description of the result
};

/**
 * Validate that a path is available for use as a navigation directory.
 *
 * For DirectoryOnly the path itself must be a readable directory.  For
 * FileOrContainingDirectory a file path is accepted and its parent directory
 * is validated.
 *
 * @param path Path to validate
 * @param mode Availability mode
 * @param expectedStorageFingerprint If non-empty, require the storage fingerprint to match
 * @return Detailed availability result
 */
PathAvailabilityResult checkPathAvailability(
    const QString& path,
    PathAvailabilityMode mode = PathAvailabilityMode::DirectoryOnly,
    const QString& expectedStorageFingerprint = QString());

/**
 * Return the first available directory from a list of candidates.
 *
 * Checks @p preferredPath first, then falls back through common writable
 * locations (home, app data, temp).  Returns an empty string if no candidate
 * is available.
 *
 * @param preferredPath Optional preferred directory to check first
 * @return Canonical path of the first available directory, or empty string
 */
QString firstAvailableDirectory(const QString& preferredPath = QString());

/**
 * Check if a file suffix represents a previewable image or video.
 *
 * @param suffix The file extension to check
 * @return true if the suffix is in the list of supported preview formats
 */
bool isPreviewableSuffix(const QString& suffix);

/**
 * Check if a file suffix represents an image file.
 *
 * @param suffix The file extension to check
 * @return true if the suffix is an image format
 */
bool isImageFile(const QString& suffix);

/**
 * Check if a file suffix represents a video file.
 *
 * @param suffix The file extension to check
 * @return true if the suffix is a video format
 */
bool isVideoFile(const QString& suffix);

} // namespace FileUtils

