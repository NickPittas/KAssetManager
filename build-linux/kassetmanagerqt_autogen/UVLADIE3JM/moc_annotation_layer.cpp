/****************************************************************************
** Meta object code from reading C++ file 'annotation_layer.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/annotation_layer.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'annotation_layer.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN15AnnotationLayerE_t {};
} // unnamed namespace

template <> constexpr inline auto AnnotationLayer::qt_create_metaobjectdata<qt_meta_tag_ZN15AnnotationLayerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AnnotationLayer",
        "annotationAdded",
        "",
        "AnnotationItem*",
        "item",
        "annotationRemoved",
        "annotationsCleared",
        "selectionChanged"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'annotationAdded'
        QtMocHelpers::SignalData<void(AnnotationItem *)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'annotationRemoved'
        QtMocHelpers::SignalData<void(AnnotationItem *)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'annotationsCleared'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectionChanged'
        QtMocHelpers::SignalData<void(AnnotationItem *)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<AnnotationLayer, qt_meta_tag_ZN15AnnotationLayerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject AnnotationLayer::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AnnotationLayerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AnnotationLayerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN15AnnotationLayerE_t>.metaTypes,
    nullptr
} };

void AnnotationLayer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AnnotationLayer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->annotationAdded((*reinterpret_cast<std::add_pointer_t<AnnotationItem*>>(_a[1]))); break;
        case 1: _t->annotationRemoved((*reinterpret_cast<std::add_pointer_t<AnnotationItem*>>(_a[1]))); break;
        case 2: _t->annotationsCleared(); break;
        case 3: _t->selectionChanged((*reinterpret_cast<std::add_pointer_t<AnnotationItem*>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AnnotationLayer::*)(AnnotationItem * )>(_a, &AnnotationLayer::annotationAdded, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AnnotationLayer::*)(AnnotationItem * )>(_a, &AnnotationLayer::annotationRemoved, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AnnotationLayer::*)()>(_a, &AnnotationLayer::annotationsCleared, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AnnotationLayer::*)(AnnotationItem * )>(_a, &AnnotationLayer::selectionChanged, 3))
            return;
    }
}

const QMetaObject *AnnotationLayer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AnnotationLayer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN15AnnotationLayerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int AnnotationLayer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void AnnotationLayer::annotationAdded(AnnotationItem * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void AnnotationLayer::annotationRemoved(AnnotationItem * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void AnnotationLayer::annotationsCleared()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AnnotationLayer::selectionChanged(AnnotationItem * _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
