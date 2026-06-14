/****************************************************************************
** Meta object code from reading C++ file 'assets_model.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../native/qt6/src/assets_model.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'assets_model.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN11AssetsModelE_t {};
} // unnamed namespace

template <> constexpr inline auto AssetsModel::qt_create_metaobjectdata<qt_meta_tag_ZN11AssetsModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "AssetsModel",
        "QML.Element",
        "auto",
        "folderIdChanged",
        "",
        "searchQueryChanged",
        "typeFilterChanged",
        "selectedTagNamesChanged",
        "tagFilterModeChanged",
        "recursiveModeChanged",
        "searchEntireDatabaseChanged",
        "tagsChangedForAsset",
        "assetId",
        "reload",
        "onAssetsChangedForFolder",
        "folderId",
        "triggerDebouncedReload",
        "setFilters",
        "typeFilter",
        "ratingFilter",
        "tagNames",
        "tagMode",
        "moveAssetToFolder",
        "moveAssetsToFolder",
        "QVariantList",
        "assetIds",
        "removeAssets",
        "setAssetsRating",
        "rating",
        "assignTags",
        "tagIds",
        "get",
        "QVariantMap",
        "row",
        "tagsForAsset",
        "searchQuery",
        "selectedTagNames",
        "tagFilterMode",
        "recursiveMode",
        "searchEntireDatabase"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'folderIdChanged'
        QtMocHelpers::SignalData<void()>(3, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'searchQueryChanged'
        QtMocHelpers::SignalData<void()>(5, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'typeFilterChanged'
        QtMocHelpers::SignalData<void()>(6, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectedTagNamesChanged'
        QtMocHelpers::SignalData<void()>(7, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'tagFilterModeChanged'
        QtMocHelpers::SignalData<void()>(8, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'recursiveModeChanged'
        QtMocHelpers::SignalData<void()>(9, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'searchEntireDatabaseChanged'
        QtMocHelpers::SignalData<void()>(10, 4, QMC::AccessPublic, QMetaType::Void),
        // Signal 'tagsChangedForAsset'
        QtMocHelpers::SignalData<void(int)>(11, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 12 },
        }}),
        // Slot 'reload'
        QtMocHelpers::SlotData<void()>(13, 4, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onAssetsChangedForFolder'
        QtMocHelpers::SlotData<void(int)>(14, 4, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Slot 'triggerDebouncedReload'
        QtMocHelpers::SlotData<void()>(16, 4, QMC::AccessPrivate, QMetaType::Void),
        // Method 'setFilters'
        QtMocHelpers::MethodData<void(int, int, const QStringList &, int)>(17, 4, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 18 }, { QMetaType::Int, 19 }, { QMetaType::QStringList, 20 }, { QMetaType::Int, 21 },
        }}),
        // Method 'moveAssetToFolder'
        QtMocHelpers::MethodData<bool(int, int)>(22, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 12 }, { QMetaType::Int, 15 },
        }}),
        // Method 'moveAssetsToFolder'
        QtMocHelpers::MethodData<bool(const QVariantList &, int)>(23, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 24, 25 }, { QMetaType::Int, 15 },
        }}),
        // Method 'removeAssets'
        QtMocHelpers::MethodData<bool(const QVariantList &)>(26, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 24, 25 },
        }}),
        // Method 'setAssetsRating'
        QtMocHelpers::MethodData<bool(const QVariantList &, int)>(27, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 24, 25 }, { QMetaType::Int, 28 },
        }}),
        // Method 'assignTags'
        QtMocHelpers::MethodData<bool(const QVariantList &, const QVariantList &)>(29, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 24, 25 }, { 0x80000000 | 24, 30 },
        }}),
        // Method 'get'
        QtMocHelpers::MethodData<QVariantMap(int) const>(31, 4, QMC::AccessPublic, 0x80000000 | 32, {{
            { QMetaType::Int, 33 },
        }}),
        // Method 'tagsForAsset'
        QtMocHelpers::MethodData<QStringList(int) const>(34, 4, QMC::AccessPublic, QMetaType::QStringList, {{
            { QMetaType::Int, 12 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'folderId'
        QtMocHelpers::PropertyData<int>(15, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'searchQuery'
        QtMocHelpers::PropertyData<QString>(35, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'typeFilter'
        QtMocHelpers::PropertyData<int>(18, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'selectedTagNames'
        QtMocHelpers::PropertyData<QStringList>(36, QMetaType::QStringList, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'tagFilterMode'
        QtMocHelpers::PropertyData<int>(37, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
        // property 'recursiveMode'
        QtMocHelpers::PropertyData<bool>(38, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 5),
        // property 'searchEntireDatabase'
        QtMocHelpers::PropertyData<bool>(39, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
    };
    QtMocHelpers::UintData qt_enums {
    };
    QtMocHelpers::UintData qt_constructors {};
    QtMocHelpers::ClassInfos qt_classinfo({
            {    1,    2 },
    });
    return QtMocHelpers::metaObjectData<AssetsModel, void>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums, qt_constructors, qt_classinfo);
}
Q_CONSTINIT const QMetaObject AssetsModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AssetsModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AssetsModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11AssetsModelE_t>.metaTypes,
    nullptr
} };

void AssetsModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<AssetsModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->folderIdChanged(); break;
        case 1: _t->searchQueryChanged(); break;
        case 2: _t->typeFilterChanged(); break;
        case 3: _t->selectedTagNamesChanged(); break;
        case 4: _t->tagFilterModeChanged(); break;
        case 5: _t->recursiveModeChanged(); break;
        case 6: _t->searchEntireDatabaseChanged(); break;
        case 7: _t->tagsChangedForAsset((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 8: _t->reload(); break;
        case 9: _t->onAssetsChangedForFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 10: _t->triggerDebouncedReload(); break;
        case 11: _t->setFilters((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[4]))); break;
        case 12: { bool _r = _t->moveAssetToFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 13: { bool _r = _t->moveAssetsToFolder((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 14: { bool _r = _t->removeAssets((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 15: { bool _r = _t->setAssetsRating((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 16: { bool _r = _t->assignTags((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 17: { QVariantMap _r = _t->get((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 18: { QStringList _r = _t->tagsForAsset((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QStringList*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::folderIdChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::searchQueryChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::typeFilterChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::selectedTagNamesChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::tagFilterModeChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::recursiveModeChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)()>(_a, &AssetsModel::searchEntireDatabaseChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (AssetsModel::*)(int )>(_a, &AssetsModel::tagsChangedForAsset, 7))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<int*>(_v) = _t->folderId(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->searchQuery(); break;
        case 2: *reinterpret_cast<int*>(_v) = _t->typeFilter(); break;
        case 3: *reinterpret_cast<QStringList*>(_v) = _t->selectedTagNames(); break;
        case 4: *reinterpret_cast<int*>(_v) = _t->tagFilterMode(); break;
        case 5: *reinterpret_cast<bool*>(_v) = _t->recursiveMode(); break;
        case 6: *reinterpret_cast<bool*>(_v) = _t->searchEntireDatabase(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setFolderId(*reinterpret_cast<int*>(_v)); break;
        case 1: _t->setSearchQuery(*reinterpret_cast<QString*>(_v)); break;
        case 2: _t->setTypeFilter(*reinterpret_cast<int*>(_v)); break;
        case 3: _t->setSelectedTagNames(*reinterpret_cast<QStringList*>(_v)); break;
        case 4: _t->setTagFilterMode(*reinterpret_cast<int*>(_v)); break;
        case 5: _t->setRecursiveMode(*reinterpret_cast<bool*>(_v)); break;
        case 6: _t->setSearchEntireDatabase(*reinterpret_cast<bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *AssetsModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *AssetsModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11AssetsModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int AssetsModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 19)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 19;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 19)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 19;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void AssetsModel::folderIdChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void AssetsModel::searchQueryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void AssetsModel::typeFilterChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void AssetsModel::selectedTagNamesChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void AssetsModel::tagFilterModeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void AssetsModel::recursiveModeChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void AssetsModel::searchEntireDatabaseChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void AssetsModel::tagsChangedForAsset(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}
QT_WARNING_POP
