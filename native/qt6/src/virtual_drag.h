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

    // Start a drag that adapts CF_HDROP payload based on drop target: frames for Explorer/self, folder(s) for DCCs
    bool startAdaptivePathsDrag(const QVector<QString> &framePaths, const QVector<QString> &folderPaths);


// Convenience single-file helper
inline bool startVirtualDragText(const QString &fileName, const QByteArray &data) {
    return startVirtualDrag(QVector<VirtualFile>{ VirtualFile{fileName, data} });
}
}
