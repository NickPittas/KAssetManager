/****************************************************************************
** Meta object code from reading C++ file 'thumbnail_generator_worker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/thumbnail_generator_worker.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'thumbnail_generator_worker.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto ThumbnailGeneratorWorker::qt_create_metaobjectdata<qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ThumbnailGeneratorWorker",
        "queueStarted",
        "",
        "totalFiles",
        "fileStarted",
        "index",
        "filePath",
        "fileProgress",
        "percent",
        "fileFinished",
        "success",
        "errorMsg",
        "queueFinished",
        "allSuccess",
        "logLine",
        "line",
        "start",
        "QList<Task>",
        "tasks",
        "QSize",
        "thumbnailSize",
        "cancelAll"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'queueStarted'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'fileStarted'
        QtMocHelpers::SignalData<void(int, const QString &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'fileProgress'
        QtMocHelpers::SignalData<void(int, int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 8 },
        }}),
        // Signal 'fileFinished'
        QtMocHelpers::SignalData<void(int, bool, const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Bool, 10 }, { QMetaType::QString, 11 },
        }}),
        // Signal 'queueFinished'
        QtMocHelpers::SignalData<void(bool)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 13 },
        }}),
        // Signal 'logLine'
        QtMocHelpers::SignalData<void(const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 15 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void(const QVector<Task> &, const QSize &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 17, 18 }, { 0x80000000 | 19, 20 },
        }}),
        // Slot 'cancelAll'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ThumbnailGeneratorWorker, qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ThumbnailGeneratorWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>.metaTypes,
    nullptr
} };

void ThumbnailGeneratorWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ThumbnailGeneratorWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->queueStarted((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->fileStarted((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 2: _t->fileProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->fileFinished((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 4: _t->queueFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->logLine((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->start((*reinterpret_cast<std::add_pointer_t<QList<Task>>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QSize>>(_a[2]))); break;
        case 7: _t->cancelAll(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(int )>(_a, &ThumbnailGeneratorWorker::queueStarted, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(int , const QString & )>(_a, &ThumbnailGeneratorWorker::fileStarted, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(int , int )>(_a, &ThumbnailGeneratorWorker::fileProgress, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(int , bool , const QString & )>(_a, &ThumbnailGeneratorWorker::fileFinished, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(bool )>(_a, &ThumbnailGeneratorWorker::queueFinished, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (ThumbnailGeneratorWorker::*)(const QString & )>(_a, &ThumbnailGeneratorWorker::logLine, 5))
            return;
    }
}

const QMetaObject *ThumbnailGeneratorWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ThumbnailGeneratorWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN24ThumbnailGeneratorWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int ThumbnailGeneratorWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void ThumbnailGeneratorWorker::queueStarted(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void ThumbnailGeneratorWorker::fileStarted(int _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2);
}

// SIGNAL 2
void ThumbnailGeneratorWorker::fileProgress(int _t1, int _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1, _t2);
}

// SIGNAL 3
void ThumbnailGeneratorWorker::fileFinished(int _t1, bool _t2, const QString & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3);
}

// SIGNAL 4
void ThumbnailGeneratorWorker::queueFinished(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void ThumbnailGeneratorWorker::logLine(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}
QT_WARNING_POP
