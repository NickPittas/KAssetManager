/****************************************************************************
** Meta object code from reading C++ file 'database_health_agent.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/database_health_agent.h"
#include <QtCore/qmetatype.h>
#include <QtCore/QList>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'database_health_agent.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN19DatabaseHealthAgentE_t {};
} // unnamed namespace

template <> constexpr inline auto DatabaseHealthAgent::qt_create_metaobjectdata<qt_meta_tag_ZN19DatabaseHealthAgentE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "DatabaseHealthAgent",
        "healthCheckStarted",
        "",
        "healthCheckProgress",
        "current",
        "total",
        "message",
        "healthCheckCompleted",
        "QList<HealthCheckResult>",
        "results",
        "maintenanceStarted",
        "operation",
        "maintenanceProgress",
        "percent",
        "maintenanceCompleted",
        "success"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'healthCheckStarted'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'healthCheckProgress'
        QtMocHelpers::SignalData<void(int, int, const QString &)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 }, { QMetaType::Int, 5 }, { QMetaType::QString, 6 },
        }}),
        // Signal 'healthCheckCompleted'
        QtMocHelpers::SignalData<void(const QVector<HealthCheckResult> &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Signal 'maintenanceStarted'
        QtMocHelpers::SignalData<void(const QString &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 11 },
        }}),
        // Signal 'maintenanceProgress'
        QtMocHelpers::SignalData<void(int)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 13 },
        }}),
        // Signal 'maintenanceCompleted'
        QtMocHelpers::SignalData<void(bool, const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 15 }, { QMetaType::QString, 6 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DatabaseHealthAgent, qt_meta_tag_ZN19DatabaseHealthAgentE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject DatabaseHealthAgent::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19DatabaseHealthAgentE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19DatabaseHealthAgentE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN19DatabaseHealthAgentE_t>.metaTypes,
    nullptr
} };

void DatabaseHealthAgent::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DatabaseHealthAgent *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->healthCheckStarted(); break;
        case 1: _t->healthCheckProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3]))); break;
        case 2: _t->healthCheckCompleted((*reinterpret_cast<std::add_pointer_t<QList<HealthCheckResult>>>(_a[1]))); break;
        case 3: _t->maintenanceStarted((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->maintenanceProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->maintenanceCompleted((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)()>(_a, &DatabaseHealthAgent::healthCheckStarted, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)(int , int , const QString & )>(_a, &DatabaseHealthAgent::healthCheckProgress, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)(const QVector<HealthCheckResult> & )>(_a, &DatabaseHealthAgent::healthCheckCompleted, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)(const QString & )>(_a, &DatabaseHealthAgent::maintenanceStarted, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)(int )>(_a, &DatabaseHealthAgent::maintenanceProgress, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (DatabaseHealthAgent::*)(bool , const QString & )>(_a, &DatabaseHealthAgent::maintenanceCompleted, 5))
            return;
    }
}

const QMetaObject *DatabaseHealthAgent::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *DatabaseHealthAgent::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN19DatabaseHealthAgentE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int DatabaseHealthAgent::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void DatabaseHealthAgent::healthCheckStarted()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void DatabaseHealthAgent::healthCheckProgress(int _t1, int _t2, const QString & _t3)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1, _t2, _t3);
}

// SIGNAL 2
void DatabaseHealthAgent::healthCheckCompleted(const QVector<HealthCheckResult> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void DatabaseHealthAgent::maintenanceStarted(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void DatabaseHealthAgent::maintenanceProgress(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void DatabaseHealthAgent::maintenanceCompleted(bool _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}
QT_WARNING_POP
