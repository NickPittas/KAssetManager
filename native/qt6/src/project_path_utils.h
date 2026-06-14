#pragma once

#include <QDir>
#include <QString>
#include <Qt>

namespace ProjectPathUtils {

inline Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

inline QString cleanPath(const QString& path)
{
    return QDir::cleanPath(path);
}

inline QString keyForPath(const QString& path)
{
    const QString cleaned = cleanPath(path);
    return pathCaseSensitivity() == Qt::CaseInsensitive ? cleaned.toLower() : cleaned;
}

inline bool pathsEqual(const QString& lhs, const QString& rhs)
{
    return cleanPath(lhs).compare(cleanPath(rhs), pathCaseSensitivity()) == 0;
}

inline bool isSameOrChildPath(const QString& parentPath, const QString& childPath)
{
    const QString parent = cleanPath(parentPath);
    const QString child = cleanPath(childPath);

    if (parent.isEmpty() || child.isEmpty()) {
        return false;
    }

    if (child.compare(parent, pathCaseSensitivity()) == 0) {
        return true;
    }

    QString prefix = parent;
    if (!prefix.endsWith('/')) {
        prefix += '/';
    }
    return child.startsWith(prefix, pathCaseSensitivity());
}

}
