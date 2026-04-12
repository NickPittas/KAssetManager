/****************************************************************************
** Meta object code from reading C++ file 'tlrender_widget.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/media/tlrender_widget.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'tlrender_widget.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14TLRenderWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto TLRenderWidget::qt_create_metaobjectdata<qt_meta_tag_ZN14TLRenderWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TLRenderWidget",
        "videoSizeChanged",
        "",
        "QSize",
        "size",
        "frameRendered",
        "renderError",
        "error",
        "onRenderTimer",
        "onPlayerPositionChanged",
        "position",
        "FitMode",
        "Fit",
        "Fill",
        "OneToOne"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'videoSizeChanged'
        QtMocHelpers::SignalData<void(const QSize &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'frameRendered'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'renderError'
        QtMocHelpers::SignalData<void(const QString &)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
        // Slot 'onRenderTimer'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPlayerPositionChanged'
        QtMocHelpers::SlotData<void(qint64)>(9, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'FitMode'
        QtMocHelpers::EnumData<enum FitMode>(11, 11, QMC::EnumIsScoped).add({
            {   12, FitMode::Fit },
            {   13, FitMode::Fill },
            {   14, FitMode::OneToOne },
        }),
    };
    return QtMocHelpers::metaObjectData<TLRenderWidget, qt_meta_tag_ZN14TLRenderWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TLRenderWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14TLRenderWidgetE_t>.metaTypes,
    nullptr
} };

void TLRenderWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TLRenderWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->videoSizeChanged((*reinterpret_cast<std::add_pointer_t<QSize>>(_a[1]))); break;
        case 1: _t->frameRendered(); break;
        case 2: _t->renderError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->onRenderTimer(); break;
        case 4: _t->onPlayerPositionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TLRenderWidget::*)(const QSize & )>(_a, &TLRenderWidget::videoSizeChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderWidget::*)()>(_a, &TLRenderWidget::frameRendered, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderWidget::*)(const QString & )>(_a, &TLRenderWidget::renderError, 2))
            return;
    }
}

const QMetaObject *TLRenderWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TLRenderWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderWidgetE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QOpenGLFunctions_4_1_Core"))
        return static_cast< QOpenGLFunctions_4_1_Core*>(this);
    return QOpenGLWidget::qt_metacast(_clname);
}

int TLRenderWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QOpenGLWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void TLRenderWidget::videoSizeChanged(const QSize & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void TLRenderWidget::frameRendered()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void TLRenderWidget::renderError(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}
QT_WARNING_POP
