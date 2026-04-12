/****************************************************************************
** Meta object code from reading C++ file 'project_assets_model.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/project_assets_model.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'project_assets_model.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN18ProjectAssetsModelE_t {};
} // unnamed namespace

template <> constexpr inline auto ProjectAssetsModel::qt_create_metaobjectdata<qt_meta_tag_ZN18ProjectAssetsModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ProjectAssetsModel",
        "projectIdChanged",
        "",
        "folderIdChanged",
        "searchQueryChanged",
        "typeFilterChanged",
        "showAllVersionsChanged",
        "reload",
        "scheduleReload",
        "get",
        "QVariantMap",
        "row",
        "getAssetIdForVersion",
        "primaryAssetId",
        "versionString",
        "removeAssets",
        "QVariantList",
        "assetIds",
        "projectId",
        "folderId",
        "searchQuery",
        "typeFilter",
        "showAllVersions"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'projectIdChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'folderIdChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'searchQueryChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'typeFilterChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'showAllVersionsChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'reload'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'scheduleReload'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Method 'get'
        QtMocHelpers::MethodData<QVariantMap(int) const>(9, 2, QMC::AccessPublic, 0x80000000 | 10, {{
            { QMetaType::Int, 11 },
        }}),
        // Method 'getAssetIdForVersion'
        QtMocHelpers::MethodData<int(int, const QString &) const>(12, 2, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 13 }, { QMetaType::QString, 14 },
        }}),
        // Method 'removeAssets'
        QtMocHelpers::MethodData<bool(const QVariantList &)>(15, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { 0x80000000 | 16, 17 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'projectId'
        QtMocHelpers::PropertyData<int>(18, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'folderId'
        QtMocHelpers::PropertyData<int>(19, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'searchQuery'
        QtMocHelpers::PropertyData<QString>(20, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'typeFilter'
        QtMocHelpers::PropertyData<int>(21, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'showAllVersions'
        QtMocHelpers::PropertyData<bool>(22, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ProjectAssetsModel, qt_meta_tag_ZN18ProjectAssetsModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ProjectAssetsModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractListModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ProjectAssetsModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ProjectAssetsModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN18ProjectAssetsModelE_t>.metaTypes,
    nullptr
} };

void ProjectAssetsModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ProjectAssetsModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->projectIdChanged(); break;
        case 1: _t->folderIdChanged(); break;
        case 2: _t->searchQueryChanged(); break;
        case 3: _t->typeFilterChanged(); break;
        case 4: _t->showAllVersionsChanged(); break;
        case 5: _t->reload(); break;
        case 6: _t->scheduleReload(); break;
        case 7: { QVariantMap _r = _t->get((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QVariantMap*>(_a[0]) = std::move(_r); }  break;
        case 8: { int _r = _t->getAssetIdForVersion((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 9: { bool _r = _t->removeAssets((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ProjectAssetsModel::*)()>(_a, &ProjectAssetsModel::projectIdChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ProjectAssetsModel::*)()>(_a, &ProjectAssetsModel::folderIdChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ProjectAssetsModel::*)()>(_a, &ProjectAssetsModel::searchQueryChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ProjectAssetsModel::*)()>(_a, &ProjectAssetsModel::typeFilterChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ProjectAssetsModel::*)()>(_a, &ProjectAssetsModel::showAllVersionsChanged, 4))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<int*>(_v) = _t->projectId(); break;
        case 1: *reinterpret_cast<int*>(_v) = _t->folderId(); break;
        case 2: *reinterpret_cast<QString*>(_v) = _t->searchQuery(); break;
        case 3: *reinterpret_cast<int*>(_v) = _t->typeFilter(); break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->showAllVersions(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setProjectId(*reinterpret_cast<int*>(_v)); break;
        case 1: _t->setFolderId(*reinterpret_cast<int*>(_v)); break;
        case 2: _t->setSearchQuery(*reinterpret_cast<QString*>(_v)); break;
        case 3: _t->setTypeFilter(*reinterpret_cast<int*>(_v)); break;
        case 4: _t->setShowAllVersions(*reinterpret_cast<bool*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *ProjectAssetsModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ProjectAssetsModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN18ProjectAssetsModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractListModel::qt_metacast(_clname);
}

int ProjectAssetsModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractListModel::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void ProjectAssetsModel::projectIdChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ProjectAssetsModel::folderIdChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void ProjectAssetsModel::searchQueryChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void ProjectAssetsModel::typeFilterChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ProjectAssetsModel::showAllVersionsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}
QT_WARNING_POP
