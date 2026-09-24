/****************************************************************************
** Meta object code from reading C++ file 'media_converter_worker.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/media_converter_worker.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'media_converter_worker.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN20MediaConverterWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto MediaConverterWorker::qt_create_metaobjectdata<qt_meta_tag_ZN20MediaConverterWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MediaConverterWorker",
        "queueStarted",
        "",
        "total",
        "fileStarted",
        "index",
        "srcPath",
        "outPath",
        "durationMs",
        "logLine",
        "line",
        "currentFileProgress",
        "percent",
        "outTimeMs",
        "totalMs",
        "overallProgress",
        "fileFinished",
        "success",
        "errorMsg",
        "queueFinished",
        "allSuccess",
        "start",
        "QList<Task>",
        "tasks",
        "cancelAll",
        "retryCurrent",
        "continueAfterFailure",
        "onReadyStdOut",
        "onReadyStdErr",
        "onFinished",
        "exitCode",
        "QProcess::ExitStatus",
        "status"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'queueStarted'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'fileStarted'
        QtMocHelpers::SignalData<void(int, const QString &, const QString &, qint64)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::QString, 6 }, { QMetaType::QString, 7 }, { QMetaType::LongLong, 8 },
        }}),
        // Signal 'logLine'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
        // Signal 'currentFileProgress'
        QtMocHelpers::SignalData<void(int, int, qint64, qint64)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Int, 12 }, { QMetaType::LongLong, 13 }, { QMetaType::LongLong, 14 },
        }}),
        // Signal 'overallProgress'
        QtMocHelpers::SignalData<void(int)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Signal 'fileFinished'
        QtMocHelpers::SignalData<void(int, bool, const QString &)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 }, { QMetaType::Bool, 17 }, { QMetaType::QString, 18 },
        }}),
        // Signal 'queueFinished'
        QtMocHelpers::SignalData<void(bool)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 20 },
        }}),
        // Slot 'start'
        QtMocHelpers::SlotData<void(const QVector<Task> &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 22, 23 },
        }}),
        // Slot 'cancelAll'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'retryCurrent'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'continueAfterFailure'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onReadyStdOut'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onReadyStdErr'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFinished'
        QtMocHelpers::SlotData<void(int, QProcess::ExitStatus)>(29, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 30 }, { 0x80000000 | 31, 32 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MediaConverterWorker, qt_meta_tag_ZN20MediaConverterWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MediaConverterWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20MediaConverterWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20MediaConverterWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN20MediaConverterWorkerE_t>.metaTypes,
    nullptr
} };

void MediaConverterWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MediaConverterWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->queueStarted((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->fileStarted((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4]))); break;
        case 2: _t->logLine((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->currentFileProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[4]))); break;
        case 4: _t->overallProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->fileFinished((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 6: _t->queueFinished((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 7: _t->start((*reinterpret_cast<std::add_pointer_t<QList<Task>>>(_a[1]))); break;
        case 8: _t->cancelAll(); break;
        case 9: _t->retryCurrent(); break;
        case 10: _t->continueAfterFailure(); break;
        case 11: _t->onReadyStdOut(); break;
        case 12: _t->onReadyStdErr(); break;
        case 13: _t->onFinished((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QProcess::ExitStatus>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(int )>(_a, &MediaConverterWorker::queueStarted, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(int , const QString & , const QString & , qint64 )>(_a, &MediaConverterWorker::fileStarted, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(const QString & )>(_a, &MediaConverterWorker::logLine, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(int , int , qint64 , qint64 )>(_a, &MediaConverterWorker::currentFileProgress, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(int )>(_a, &MediaConverterWorker::overallProgress, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(int , bool , const QString & )>(_a, &MediaConverterWorker::fileFinished, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MediaConverterWorker::*)(bool )>(_a, &MediaConverterWorker::queueFinished, 6))
            return;
    }
}

const QMetaObject *MediaConverterWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MediaConverterWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN20MediaConverterWorkerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MediaConverterWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void MediaConverterWorker::queueStarted(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void MediaConverterWorker::fileStarted(int _t1, const QString & _t2, const QString & _t3, qint64 _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 2
void MediaConverterWorker::logLine(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void MediaConverterWorker::currentFileProgress(int _t1, int _t2, qint64 _t3, qint64 _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 4
void MediaConverterWorker::overallProgress(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void MediaConverterWorker::fileFinished(int _t1, bool _t2, const QString & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2, _t3);
}

// SIGNAL 6
void MediaConverterWorker::queueFinished(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}
QT_WARNING_POP
