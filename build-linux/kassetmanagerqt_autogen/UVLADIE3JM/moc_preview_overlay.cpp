/****************************************************************************
** Meta object code from reading C++ file 'preview_overlay.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/preview_overlay.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/QSet>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'preview_overlay.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN17CachedFrameSliderE_t {};
} // unnamed namespace

template <> constexpr inline auto CachedFrameSlider::qt_create_metaobjectdata<qt_meta_tag_ZN17CachedFrameSliderE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CachedFrameSlider"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CachedFrameSlider, qt_meta_tag_ZN17CachedFrameSliderE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CachedFrameSlider::staticMetaObject = { {
    QMetaObject::SuperData::link<QSlider::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CachedFrameSliderE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CachedFrameSliderE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17CachedFrameSliderE_t>.metaTypes,
    nullptr
} };

void CachedFrameSlider::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CachedFrameSlider *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *CachedFrameSlider::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CachedFrameSlider::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17CachedFrameSliderE_t>.strings))
        return static_cast<void*>(this);
    return QSlider::qt_metacast(_clname);
}

int CachedFrameSlider::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QSlider::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN14CacheBarWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto CacheBarWidget::qt_create_metaobjectdata<qt_meta_tag_ZN14CacheBarWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "CacheBarWidget"
    };

    QtMocHelpers::UintData qt_methods {
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<CacheBarWidget, qt_meta_tag_ZN14CacheBarWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject CacheBarWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14CacheBarWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14CacheBarWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14CacheBarWidgetE_t>.metaTypes,
    nullptr
} };

void CacheBarWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<CacheBarWidget *>(_o);
    (void)_t;
    (void)_c;
    (void)_id;
    (void)_a;
}

const QMetaObject *CacheBarWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CacheBarWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14CacheBarWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int CacheBarWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    return _id;
}
namespace {
struct qt_meta_tag_ZN14PreviewOverlayE_t {};
} // unnamed namespace

template <> constexpr inline auto PreviewOverlay::qt_create_metaobjectdata<qt_meta_tag_ZN14PreviewOverlayE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "PreviewOverlay",
        "closed",
        "",
        "navigateRequested",
        "delta",
        "onPlayPauseClicked",
        "onPositionChanged",
        "position",
        "onDurationChanged",
        "duration",
        "onSliderMoved",
        "onSliderPressed",
        "onSliderReleased",
        "onStepPrevFrame",
        "onStepNextFrame",
        "onVolumeChanged",
        "value",
        "onToggleMute",
        "hideControls",
        "onSequenceTimerTick",
        "onColorSpaceChanged",
        "index",
        "onPlayerPositionChanged",
        "positionMs",
        "onPlayerDurationChanged",
        "durationMs",
        "onPlayerMediaInfo",
        "TLRenderPlayer::MediaInfo",
        "info",
        "onPlayerPlaybackStateChanged",
        "TLRenderPlayer::PlaybackState",
        "state",
        "onPlayerError",
        "errorString",
        "onPlayerEndOfStream",
        "onToggleAnnotation",
        "onAnnotationToolSelected",
        "onColorPicker",
        "onPenWidthChanged",
        "width",
        "onClearAnnotations",
        "onSaveAnnotatedFrame",
        "onSaveAllAnnotatedFrames"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'closed'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'navigateRequested'
        QtMocHelpers::SignalData<void(int)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Slot 'onPlayPauseClicked'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPositionChanged'
        QtMocHelpers::SlotData<void(qint64)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 7 },
        }}),
        // Slot 'onDurationChanged'
        QtMocHelpers::SlotData<void(qint64)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 9 },
        }}),
        // Slot 'onSliderMoved'
        QtMocHelpers::SlotData<void(int)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 7 },
        }}),
        // Slot 'onSliderPressed'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSliderReleased'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStepPrevFrame'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStepNextFrame'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onVolumeChanged'
        QtMocHelpers::SlotData<void(int)>(15, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 16 },
        }}),
        // Slot 'onToggleMute'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'hideControls'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSequenceTimerTick'
        QtMocHelpers::SlotData<void()>(19, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onColorSpaceChanged'
        QtMocHelpers::SlotData<void(int)>(20, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 21 },
        }}),
        // Slot 'onPlayerPositionChanged'
        QtMocHelpers::SlotData<void(qint64)>(22, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 23 },
        }}),
        // Slot 'onPlayerDurationChanged'
        QtMocHelpers::SlotData<void(qint64)>(24, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 25 },
        }}),
        // Slot 'onPlayerMediaInfo'
        QtMocHelpers::SlotData<void(const TLRenderPlayer::MediaInfo &)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 27, 28 },
        }}),
        // Slot 'onPlayerPlaybackStateChanged'
        QtMocHelpers::SlotData<void(TLRenderPlayer::PlaybackState)>(29, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 30, 31 },
        }}),
        // Slot 'onPlayerError'
        QtMocHelpers::SlotData<void(const QString &)>(32, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 33 },
        }}),
        // Slot 'onPlayerEndOfStream'
        QtMocHelpers::SlotData<void()>(34, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onToggleAnnotation'
        QtMocHelpers::SlotData<void()>(35, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAnnotationToolSelected'
        QtMocHelpers::SlotData<void()>(36, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onColorPicker'
        QtMocHelpers::SlotData<void()>(37, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPenWidthChanged'
        QtMocHelpers::SlotData<void(int)>(38, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 39 },
        }}),
        // Slot 'onClearAnnotations'
        QtMocHelpers::SlotData<void()>(40, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveAnnotatedFrame'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSaveAllAnnotatedFrames'
        QtMocHelpers::SlotData<void()>(42, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PreviewOverlay, qt_meta_tag_ZN14PreviewOverlayE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject PreviewOverlay::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreviewOverlayE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreviewOverlayE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14PreviewOverlayE_t>.metaTypes,
    nullptr
} };

void PreviewOverlay::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PreviewOverlay *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->closed(); break;
        case 1: _t->navigateRequested((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->onPlayPauseClicked(); break;
        case 3: _t->onPositionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 4: _t->onDurationChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 5: _t->onSliderMoved((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->onSliderPressed(); break;
        case 7: _t->onSliderReleased(); break;
        case 8: _t->onStepPrevFrame(); break;
        case 9: _t->onStepNextFrame(); break;
        case 10: _t->onVolumeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 11: _t->onToggleMute(); break;
        case 12: _t->hideControls(); break;
        case 13: _t->onSequenceTimerTick(); break;
        case 14: _t->onColorSpaceChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 15: _t->onPlayerPositionChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 16: _t->onPlayerDurationChanged((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1]))); break;
        case 17: _t->onPlayerMediaInfo((*reinterpret_cast<std::add_pointer_t<TLRenderPlayer::MediaInfo>>(_a[1]))); break;
        case 18: _t->onPlayerPlaybackStateChanged((*reinterpret_cast<std::add_pointer_t<TLRenderPlayer::PlaybackState>>(_a[1]))); break;
        case 19: _t->onPlayerError((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 20: _t->onPlayerEndOfStream(); break;
        case 21: _t->onToggleAnnotation(); break;
        case 22: _t->onAnnotationToolSelected(); break;
        case 23: _t->onColorPicker(); break;
        case 24: _t->onPenWidthChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 25: _t->onClearAnnotations(); break;
        case 26: _t->onSaveAnnotatedFrame(); break;
        case 27: _t->onSaveAllAnnotatedFrames(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PreviewOverlay::*)()>(_a, &PreviewOverlay::closed, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PreviewOverlay::*)(int )>(_a, &PreviewOverlay::navigateRequested, 1))
            return;
    }
}

const QMetaObject *PreviewOverlay::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PreviewOverlay::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14PreviewOverlayE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int PreviewOverlay::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 28)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 28;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 28)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 28;
    }
    return _id;
}

// SIGNAL 0
void PreviewOverlay::closed()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void PreviewOverlay::navigateRequested(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN18SequenceFrameCacheE_t {};
} // unnamed namespace

template <> constexpr inline auto SequenceFrameCache::qt_create_metaobjectdata<qt_meta_tag_ZN18SequenceFrameCacheE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "SequenceFrameCache",
        "frameCached",
        "",
        "frameIndex",
        "cacheSnapshot",
        "QSet<int>",
        "frames"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'frameCached'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Signal 'cacheSnapshot'
        QtMocHelpers::SignalData<void(const QSet<int> &)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 5, 6 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<SequenceFrameCache, qt_meta_tag_ZN18SequenceFrameCacheE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject SequenceFrameCache::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18SequenceFrameCacheE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18SequenceFrameCacheE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18SequenceFrameCacheE_t>.metaTypes,
    nullptr
} };

void SequenceFrameCache::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<SequenceFrameCache *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->frameCached((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->cacheSnapshot((*reinterpret_cast<std::add_pointer_t<QSet<int>>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< QSet<int> >(); break;
            }
            break;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (SequenceFrameCache::*)(int )>(_a, &SequenceFrameCache::frameCached, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (SequenceFrameCache::*)(const QSet<int> & )>(_a, &SequenceFrameCache::cacheSnapshot, 1))
            return;
    }
}

const QMetaObject *SequenceFrameCache::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *SequenceFrameCache::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18SequenceFrameCacheE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int SequenceFrameCache::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void SequenceFrameCache::frameCached(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void SequenceFrameCache::cacheSnapshot(const QSet<int> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
namespace {
struct qt_meta_tag_ZN17FrameLoaderWorkerE_t {};
} // unnamed namespace

template <> constexpr inline auto FrameLoaderWorker::qt_create_metaobjectdata<qt_meta_tag_ZN17FrameLoaderWorkerE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "FrameLoaderWorker",
        "frameLoaded",
        "",
        "frameIndex",
        "QImage",
        "image"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'frameLoaded'
        QtMocHelpers::SignalData<void(int, QImage)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 }, { 0x80000000 | 4, 5 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<FrameLoaderWorker, qt_meta_tag_ZN17FrameLoaderWorkerE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject FrameLoaderWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17FrameLoaderWorkerE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17FrameLoaderWorkerE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN17FrameLoaderWorkerE_t>.metaTypes,
    nullptr
} };

void FrameLoaderWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<FrameLoaderWorker *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->frameLoaded((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QImage>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (FrameLoaderWorker::*)(int , QImage )>(_a, &FrameLoaderWorker::frameLoaded, 0))
            return;
    }
}

const QMetaObject *FrameLoaderWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *FrameLoaderWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN17FrameLoaderWorkerE_t>.strings))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QRunnable"))
        return static_cast< QRunnable*>(this);
    return QObject::qt_metacast(_clname);
}

int FrameLoaderWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void FrameLoaderWorker::frameLoaded(int _t1, QImage _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}
QT_WARNING_POP
