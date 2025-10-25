#pragma once
#include <QObject>
#include <QStringList>

class DragUtils : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    bool startFileDrag(const QStringList &paths);
    bool startVirtualDragSample();
    bool startVirtualDragSampleMulti();
    bool startVirtualDragSampleFallbackCFHDrop();
    bool showInExplorer(const QString &path);

    static DragUtils& instance();
};

