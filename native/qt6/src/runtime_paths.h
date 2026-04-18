#pragma once

#include <QString>

namespace RuntimePaths {

QString portableDataRoot();
QString userDataRoot();
QString writableDataRoot();
QString dataPath(const QString& relativePath = QString());
bool usingPortableDataRoot();
void migrateLegacyDataToWritableRoot();

} // namespace RuntimePaths
