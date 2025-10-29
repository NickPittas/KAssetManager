#pragma once

#include <QString>

class QStandardItemModel;

// Extract plain text from DOCX (word/document.xml). Returns empty on failure.
QString extractDocxText(const QString& filePath);

// Load first XLSX worksheet into model. Caps rows by maxRows. Returns false on failure.
bool loadXlsxSheet(const QString& filePath, QStandardItemModel* model, int maxRows = 2000);

// Extract best-effort plain text from legacy .doc (binary) by scanning for UTF-16LE/ASCII runs.
QString extractDocBinaryText(const QString& filePath, int maxChars = 2000000);


