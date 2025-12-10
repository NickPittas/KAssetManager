#pragma once
#include <QString>
#include <QVector>
#include <QPair>
#include <QRegularExpression>

/**
 * @brief Parsed version information from a filename.
 */
struct VersionInfo {
    QString baseName;           // Filename without version and extension
    QString versionString;      // The extracted version string (e.g., "v01", "001", "1")
    int versionNumber = 0;      // Numeric version value for sorting
    QString extension;          // File extension (lowercase)
    QString fullPath;           // Original full path
    QString versionGroupKey;    // Key for grouping versions together (baseName + extension)
};

/**
 * @brief Detects and parses version information from After Effects and Nuke project files.
 * 
 * Supports various version naming conventions:
 * - Standard: filename_v001.aep, filename_v01.nk
 * - Numeric only: filename_001.aep, filename_01.aep, filename_1.aep
 * - Separator variations: filename-v001.aep, filename v001.aep
 * 
 * AE files: .aep, .aepx
 * Nuke files: .nk
 */
class ProjectVersionDetector {
public:
    /**
     * @brief Check if a file extension is a supported project file.
     */
    static bool isProjectFile(const QString& filePath);

    /**
     * @brief Check if a file is an After Effects project.
     */
    static bool isAfterEffectsFile(const QString& filePath);

    /**
     * @brief Check if a file is a Nuke project.
     */
    static bool isNukeFile(const QString& filePath);

    /**
     * @brief Parse version information from a file path.
     * @param filePath Full path to the file
     * @return VersionInfo with parsed data, or empty if no version detected
     */
    static VersionInfo parseVersion(const QString& filePath);

    /**
     * @brief Group files by their base name (without version).
     * @param filePaths List of file paths to analyze
     * @return Map of versionGroupKey -> list of (versionNumber, filePath) sorted descending by version
     */
    static QMap<QString, QVector<QPair<int, QString>>> groupByVersion(const QStringList& filePaths);

    /**
     * @brief Get the highest version file from a version group.
     * @param versions List of (versionNumber, filePath) pairs
     * @return FilePath of the highest version, or empty if list is empty
     */
    static QString getHighestVersionPath(const QVector<QPair<int, QString>>& versions);

private:
    // Regex patterns for version detection
    // Pattern 1: _v### or -v### or .v### (with 'v' prefix)
    // Pattern 2: _### or -### or .### (numeric only, must be at end before extension)
    static QRegularExpression s_versionWithVRegex;
    static QRegularExpression s_versionNumericRegex;
};
