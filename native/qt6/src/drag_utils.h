#pragma once
#include <QObject>
#include <QStringList>
#include <QtQml/qqml.h>

class QQmlEngine;
class QJSEngine;

class DragUtils : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    using QObject::QObject;
    Q_INVOKABLE bool startFileDrag(const QStringList &paths);
    Q_INVOKABLE bool startVirtualDragSample();
    Q_INVOKABLE bool startVirtualDragSampleMulti();
    Q_INVOKABLE bool startVirtualDragSampleFallbackCFHDrop();
    Q_INVOKABLE bool showInExplorer(const QString &path);

    static DragUtils* create(QQmlEngine* engine, QJSEngine* jsEngine);
};

