/****************************************************************************
** Meta object code from reading C++ file 'virtual_folders.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/virtual_folders.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'virtual_folders.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN22VirtualFolderTreeModelE_t {};
} // unnamed namespace

template <> constexpr inline auto VirtualFolderTreeModel::qt_create_metaobjectdata<qt_meta_tag_ZN22VirtualFolderTreeModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "VirtualFolderTreeModel",
        "QML.Element",
        "auto",
        "reload",
        "",
        "rootId",
        "createFolder",
        "parentId",
        "name",
        "renameFolder",
        "id",
        "deleteFolder",
        "moveFolder",
        "newParentId",
        "nodeIdAt",
        "row",
        "nodeName",
        "isProjectFolder",
        "getProjectFolderId",
        "virtualFolderId"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'reload'
        QtMocHelpers::SlotData<void()>(3, 4, QMC::AccessPublic, QMetaType::Void),
        // Method 'rootId'
        QtMocHelpers::MethodData<int() const>(5, 4, QMC::AccessPublic, QMetaType::Int),
        // Method 'createFolder'
        QtMocHelpers::MethodData<int(int, const QString &)>(6, 4, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 7 }, { QMetaType::QString, 8 },
        }}),
        // Method 'renameFolder'
        QtMocHelpers::MethodData<bool(int, const QString &)>(9, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 10 }, { QMetaType::QString, 8 },
        }}),
        // Method 'deleteFolder'
        QtMocHelpers::MethodData<bool(int)>(11, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 10 },
        }}),
        // Method 'moveFolder'
        QtMocHelpers::MethodData<bool(int, int)>(12, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 10 }, { QMetaType::Int, 13 },
        }}),
        // Method 'nodeIdAt'
        QtMocHelpers::MethodData<int(int, int) const>(14, 4, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 15 }, { QMetaType::Int, 7 },
        }}),
        // Method 'nodeName'
        QtMocHelpers::MethodData<QString(int) const>(16, 4, QMC::AccessPublic, QMetaType::QString, {{
            { QMetaType::Int, 10 },
        }}),
        // Method 'isProjectFolder'
        QtMocHelpers::MethodData<bool(int) const>(17, 4, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::Int, 10 },
        }}),
        // Method 'getProjectFolderId'
        QtMocHelpers::MethodData<int(int) const>(18, 4, QMC::AccessPublic, QMetaType::Int, {{
            { QMetaType::Int, 19 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    QtMocHelpers::UintData qt_constructors {};
    QtMocHelpers::ClassInfos qt_classinfo({
            {    1,    2 },
    });
    return QtMocHelpers::metaObjectData<VirtualFolderTreeModel, void>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums, qt_constructors, qt_classinfo);
}
Q_CONSTINIT const QMetaObject VirtualFolderTreeModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractItemModel::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22VirtualFolderTreeModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22VirtualFolderTreeModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN22VirtualFolderTreeModelE_t>.metaTypes,
    nullptr
} };

void VirtualFolderTreeModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<VirtualFolderTreeModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->reload(); break;
        case 1: { int _r = _t->rootId();
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 2: { int _r = _t->createFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 3: { bool _r = _t->renameFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 4: { bool _r = _t->deleteFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 5: { bool _r = _t->moveFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 6: { int _r = _t->nodeIdAt((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        case 7: { QString _r = _t->nodeName((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<QString*>(_a[0]) = std::move(_r); }  break;
        case 8: { bool _r = _t->isProjectFolder((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 9: { int _r = _t->getProjectFolderId((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])));
            if (_a[0]) *reinterpret_cast<int*>(_a[0]) = std::move(_r); }  break;
        default: ;
        }
    }
}

const QMetaObject *VirtualFolderTreeModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *VirtualFolderTreeModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN22VirtualFolderTreeModelE_t>.strings))
        return static_cast<void*>(this);
    return QAbstractItemModel::qt_metacast(_clname);
}

int VirtualFolderTreeModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractItemModel::qt_metacall(_c, _id, _a);
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
    return _id;
}
QT_WARNING_POP
