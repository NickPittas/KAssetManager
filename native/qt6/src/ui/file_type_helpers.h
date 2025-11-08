#pragma once

#include <QSet>
#include <QString>

// Common helpers for checking file types across the UI.
bool isImageFile(const QString& ext);
bool isVideoFile(const QString& ext);
bool isAudioFile(const QString& ext);
bool isPdfFile(const QString& ext);
bool isSvgFile(const QString& ext);
bool isTextFile(const QString& ext);
bool isCsvFile(const QString& ext);
bool isExcelFile(const QString& ext);
bool isDocxFile(const QString& ext);
bool isDocFile(const QString& ext);
bool isAiFile(const QString& ext);
bool isPptxFile(const QString& ext);
