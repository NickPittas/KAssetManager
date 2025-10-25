#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>

namespace VirtualDrag {
struct VirtualFile { QString name; QByteArray data; };

// Start a Windows virtual-file drag using FILEDESCRIPTORW/FILECONTENTS for one or more in-memory files.
bool startVirtualDrag(const QVector<VirtualFile> &files);

// Start a Windows drag for existing real file paths using CF_HDROP
bool startRealPathsDrag(const QVector<QString> &paths);

// Convenience single-file helper
inline bool startVirtualDragText(const QString &fileName, const QByteArray &data) {
    return startVirtualDrag(QVector<VirtualFile>{ VirtualFile{fileName, data} });
}
}
