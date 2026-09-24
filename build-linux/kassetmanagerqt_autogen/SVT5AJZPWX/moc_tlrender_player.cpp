/****************************************************************************
** Meta object code from reading C++ file 'tlrender_player.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/media/tlrender_player.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'tlrender_player.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14TLRenderPlayerE_t {};
} // unnamed namespace

template <> constexpr inline auto TLRenderPlayer::qt_create_metaobjectdata<qt_meta_tag_ZN14TLRenderPlayerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TLRenderPlayer",
        "videoFramesChanged",
        "",
        "playbackStateChanged",
        "TLRenderPlayer::PlaybackState",
        "state",
        "positionChanged",
        "positionMs",
        "durationChanged",
        "durationMs",
        "currentFrameChanged",
        "frameNumber",
        "mediaInfoReady",
        "TLRenderPlayer::MediaInfo",
        "info",
        "error",
        "errorString",
        "endOfStream",
        "ocioConfigChanged",
        "configPath",
        "ocioOptionsChanged",
        "colorspacesChanged",
        "colorspaces",
        "displaysChanged",
        "displays",
        "viewsChanged",
        "views",
        "onUpdateTimer",
        "PlaybackState",
        "Stopped",
        "Playing",
        "Paused",
        "LoopMode",
        "Once",
        "Loop",
        "PingPong"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'videoFramesChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'playbackStateChanged'
        QtMocHelpers::SignalData<void(TLRenderPlayer::PlaybackState)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 4, 5 },
        }}),
        // Signal 'positionChanged'
        QtMocHelpers::SignalData<void(qint64)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 7 },
        }}),
        // Signal 'durationChanged'
        QtMocHelpers::SignalData<void(qint64)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 9 },
        }}),
        // Signal 'currentFrameChanged'
        QtMocHelpers::SignalData<void(qint64)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::LongLong, 11 },
        }}),
        // Signal 'mediaInfoReady'
        QtMocHelpers::SignalData<void(const TLRenderPlayer::MediaInfo &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 13, 14 },
        }}),
        // Signal 'error'
        QtMocHelpers::SignalData<void(const QString &)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 16 },
        }}),
        // Signal 'endOfStream'
        QtMocHelpers::SignalData<void()>(17, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'ocioConfigChanged'
        QtMocHelpers::SignalData<void(const QString &)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 19 },
        }}),
        // Signal 'ocioOptionsChanged'
        QtMocHelpers::SignalData<void()>(20, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'colorspacesChanged'
        QtMocHelpers::SignalData<void(const QStringList &)>(21, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 22 },
        }}),
        // Signal 'displaysChanged'
        QtMocHelpers::SignalData<void(const QStringList &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 24 },
        }}),
        // Signal 'viewsChanged'
        QtMocHelpers::SignalData<void(const QStringList &)>(25, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 26 },
        }}),
        // Slot 'onUpdateTimer'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
        // enum 'PlaybackState'
        QtMocHelpers::EnumData<enum PlaybackState>(28, 28, QMC::EnumIsScoped).add({
            {   29, PlaybackState::Stopped },
            {   30, PlaybackState::Playing },
            {   31, PlaybackState::Paused },
        }),
        // enum 'LoopMode'
        QtMocHelpers::EnumData<enum LoopMode>(32, 32, QMC::EnumIsScoped).add({
            {   33, LoopMode::Once },
            {   34, LoopMode::Loop },
            {   35, LoopMode::PingPong },
        }),
    };
    return QtMocHelpers::metaObjectData<TLRenderPlayer, qt_meta_tag_ZN14TLRenderPlayerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TLRenderPlayer::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderPlayerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderPlayerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14TLRenderPlayerE_t>.metaTypes,
    nullptr
} };

void TLRenderPlayer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TLRenderPlayer *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->videoFramesChanged(); break;
        case 1: _t->playbackStateChanged((*reinterpret_cast<std::add_pointer_t<TLRenderPlayer::PlaybackState>>(_a[1]))); break;
        case 2: _t->positionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 3: _t->durationChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 4: _t->currentFrameChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 5: _t->mediaInfoReady((*reinterpret_cast<std::add_pointer_t<TLRenderPlayer::MediaInfo>>(_a[1]))); break;
        case 6: _t->error((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 7: _t->endOfStream(); break;
        case 8: _t->ocioConfigChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 9: _t->ocioOptionsChanged(); break;
        case 10: _t->colorspacesChanged((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 11: _t->displaysChanged((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 12: _t->viewsChanged((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 13: _t->onUpdateTimer(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)()>(_a, &TLRenderPlayer::videoFramesChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(TLRenderPlayer::PlaybackState )>(_a, &TLRenderPlayer::playbackStateChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(qint64 )>(_a, &TLRenderPlayer::positionChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(qint64 )>(_a, &TLRenderPlayer::durationChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(qint64 )>(_a, &TLRenderPlayer::currentFrameChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const TLRenderPlayer::MediaInfo & )>(_a, &TLRenderPlayer::mediaInfoReady, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const QString & )>(_a, &TLRenderPlayer::error, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)()>(_a, &TLRenderPlayer::endOfStream, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const QString & )>(_a, &TLRenderPlayer::ocioConfigChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)()>(_a, &TLRenderPlayer::ocioOptionsChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const QStringList & )>(_a, &TLRenderPlayer::colorspacesChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const QStringList & )>(_a, &TLRenderPlayer::displaysChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (TLRenderPlayer::*)(const QStringList & )>(_a, &TLRenderPlayer::viewsChanged, 12))
            return;
    }
}

const QMetaObject *TLRenderPlayer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TLRenderPlayer::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TLRenderPlayerE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int TLRenderPlayer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
void TLRenderPlayer::videoFramesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void TLRenderPlayer::playbackStateChanged(TLRenderPlayer::PlaybackState _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void TLRenderPlayer::positionChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void TLRenderPlayer::durationChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void TLRenderPlayer::currentFrameChanged(qint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void TLRenderPlayer::mediaInfoReady(const TLRenderPlayer::MediaInfo & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void TLRenderPlayer::error(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void TLRenderPlayer::endOfStream()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void TLRenderPlayer::ocioConfigChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void TLRenderPlayer::ocioOptionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 9, nullptr);
}

// SIGNAL 10
void TLRenderPlayer::colorspacesChanged(const QStringList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void TLRenderPlayer::displaysChanged(const QStringList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1);
}

// SIGNAL 12
void TLRenderPlayer::viewsChanged(const QStringList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}
QT_WARNING_POP
