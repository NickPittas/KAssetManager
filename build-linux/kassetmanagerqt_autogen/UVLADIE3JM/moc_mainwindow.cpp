/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../native/qt6/src/mainwindow.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN10MainWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto MainWindow::qt_create_metaobjectdata<qt_meta_tag_ZN10MainWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MainWindow",
        "onFolderSelected",
        "",
        "QModelIndex",
        "index",
        "onAssetSelectionChanged",
        "onAssetDoubleClicked",
        "onAssetContextMenu",
        "QPoint",
        "pos",
        "onFolderContextMenu",
        "onEmptySpaceContextMenu",
        "showPreview",
        "closePreview",
        "changePreview",
        "delta",
        "applyFilters",
        "clearFilters",
        "onSearchTextChanged",
        "text",
        "onCreateTag",
        "onApplyTags",
        "onFilterByTags",
        "onTagContextMenu",
        "updateTagButtonStates",
        "onOpenSettings",
        "onThumbnailSizeChanged",
        "size",
        "onViewModeChanged",
        "importFiles",
        "filePaths",
        "onImportProgress",
        "current",
        "total",
        "onImportFileChanged",
        "fileName",
        "onImportFolderChanged",
        "folderName",
        "onImportComplete",
        "onRatingChanged",
        "rating",
        "onPrefetchLivePreviewsForFolder",
        "onRefreshLivePreviewsForFolder",
        "onPrefetchLivePreviewsRecursive",
        "onRefreshLivePreviewsRecursive",
        "onAddProjectFolder",
        "onRefreshAssets",
        "onLockToggled",
        "checked",
        "onProjectFolderChanged",
        "projectFolderId",
        "path",
        "onRevertSelectedVersion",
        "onAssetVersionsChanged",
        "assetId",
        "onToggleLogViewer",
        "showDatabaseHealthDialog",
        "performStartupHealthCheck",
        "onTabChanged",
        "onPmProjectSelected",
        "onPmFolderSelected",
        "onPmAssetSelectionChanged",
        "onPmAssetDoubleClicked",
        "onPmAssetContextMenu",
        "onPmProjectContextMenu",
        "onPmFolderContextMenu",
        "onPmCreateProject",
        "pmImportToProject",
        "name",
        "watchPath",
        "onPmRenameProject",
        "onPmDeleteProject",
        "onPmAddWatchFolder",
        "onPmViewModeToggled",
        "onPmThumbnailSizeChanged",
        "onPmToggleShowAllVersions",
        "onPmVersionSelected",
        "versionPath",
        "onPmVersionDropdownRequested",
        "globalPos",
        "onPmRefresh",
        "generateProjectThumbnails",
        "projectId",
        "forceRefresh",
        "onPmMarkNotificationsRead",
        "onPmShowNotifications",
        "onPmNewFilesDetected",
        "newFiles",
        "onPmFilesRemoved",
        "removedFiles",
        "onPmOpenOverlay",
        "changePmPreview",
        "onPmNavigateBack",
        "onPmNavigateUp",
        "onPmCopy",
        "onPmCut",
        "onPmPaste",
        "onPmDelete",
        "onPmRename",
        "onPmNewFolder",
        "onPmOpenExternal",
        "onPmTogglePreview",
        "onPmGroupSequencesToggled",
        "onShowUserGuide",
        "onAboutKAssetManager",
        "onAssetNavigateBack",
        "onAssetNavigateUp",
        "onAssetNewFolder",
        "onAssetGroupSequencesToggled",
        "onAssetFoldersModelAboutToReset",
        "onAssetFoldersModelReset",
        "onFmTreeCurrentChanged",
        "previous",
        "onFmTreeActivated",
        "onFmTreeContextMenu",
        "onFmItemDoubleClicked",
        "onFmViewModeToggled",
        "onFmThumbnailSizeChanged",
        "onAddSelectionToAssetLibrary",
        "onAddTreeSelectionToAssetLibrary",
        "onFmAddToFavorites",
        "onFmRemoveFavorite",
        "onFmFavoriteActivated",
        "QListWidgetItem*",
        "item",
        "onFmNavigateBack",
        "onFmNavigateUp",
        "onFmCopy",
        "onFmCut",
        "onFmPaste",
        "onFmDelete",
        "onFmDeletePermanent",
        "onFmRename",
        "onFmBulkRename",
        "onFmNewFolder",
        "onFmCreateFolderWithSelected",
        "onFmShowContextMenu",
        "onFmBackToParent",
        "onFmRefresh",
        "onFmLightRefresh",
        "onFmGroupSequencesToggled",
        "onFmHideFoldersToggled",
        "onFmToggleSecondPane",
        "onFmSyncNavToggled",
        "onEverythingSearchAssetManager",
        "onEverythingSearchFileManager",
        "onEverythingImportRequested",
        "paths",
        "onFmSelectionChanged",
        "onFmTogglePreview",
        "onFmOpenOverlay",
        "changeFmPreview",
        "applyTheme",
        "onFmTreeChildrenFetched",
        "parent"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'onFolderSelected'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(1, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onAssetSelectionChanged'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetDoubleClicked'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onAssetContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(7, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onFolderContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(10, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onEmptySpaceContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(11, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'showPreview'
        QtMocHelpers::SlotData<void(int)>(12, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Slot 'closePreview'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'changePreview'
        QtMocHelpers::SlotData<void(int)>(14, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Slot 'applyFilters'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'clearFilters'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSearchTextChanged'
        QtMocHelpers::SlotData<void(const QString &)>(18, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 19 },
        }}),
        // Slot 'onCreateTag'
        QtMocHelpers::SlotData<void()>(20, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onApplyTags'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFilterByTags'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTagContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(23, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'updateTagButtonStates'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onOpenSettings'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onThumbnailSizeChanged'
        QtMocHelpers::SlotData<void(int)>(26, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 27 },
        }}),
        // Slot 'onViewModeChanged'
        QtMocHelpers::SlotData<void()>(28, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'importFiles'
        QtMocHelpers::SlotData<void(const QStringList &)>(29, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QStringList, 30 },
        }}),
        // Slot 'onImportProgress'
        QtMocHelpers::SlotData<void(int, int)>(31, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 32 }, { QMetaType::Int, 33 },
        }}),
        // Slot 'onImportFileChanged'
        QtMocHelpers::SlotData<void(const QString &)>(34, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 35 },
        }}),
        // Slot 'onImportFolderChanged'
        QtMocHelpers::SlotData<void(const QString &)>(36, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 37 },
        }}),
        // Slot 'onImportComplete'
        QtMocHelpers::SlotData<void()>(38, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRatingChanged'
        QtMocHelpers::SlotData<void(int)>(39, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 40 },
        }}),
        // Slot 'onPrefetchLivePreviewsForFolder'
        QtMocHelpers::SlotData<void()>(41, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRefreshLivePreviewsForFolder'
        QtMocHelpers::SlotData<void()>(42, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPrefetchLivePreviewsRecursive'
        QtMocHelpers::SlotData<void()>(43, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRefreshLivePreviewsRecursive'
        QtMocHelpers::SlotData<void()>(44, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddProjectFolder'
        QtMocHelpers::SlotData<void()>(45, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRefreshAssets'
        QtMocHelpers::SlotData<void()>(46, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onLockToggled'
        QtMocHelpers::SlotData<void(bool)>(47, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onProjectFolderChanged'
        QtMocHelpers::SlotData<void(int, const QString &)>(49, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 50 }, { QMetaType::QString, 51 },
        }}),
        // Slot 'onRevertSelectedVersion'
        QtMocHelpers::SlotData<void()>(52, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetVersionsChanged'
        QtMocHelpers::SlotData<void(int)>(53, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 54 },
        }}),
        // Slot 'onToggleLogViewer'
        QtMocHelpers::SlotData<void()>(55, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'showDatabaseHealthDialog'
        QtMocHelpers::SlotData<void()>(56, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'performStartupHealthCheck'
        QtMocHelpers::SlotData<void()>(57, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTabChanged'
        QtMocHelpers::SlotData<void(int)>(58, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 4 },
        }}),
        // Slot 'onPmProjectSelected'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(59, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onPmFolderSelected'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(60, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onPmAssetSelectionChanged'
        QtMocHelpers::SlotData<void()>(61, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmAssetDoubleClicked'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(62, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onPmAssetContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(63, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onPmProjectContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(64, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onPmFolderContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(65, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onPmCreateProject'
        QtMocHelpers::SlotData<void()>(66, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'pmImportToProject'
        QtMocHelpers::SlotData<void(const QString &, const QString &)>(67, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 68 }, { QMetaType::QString, 69 },
        }}),
        // Slot 'onPmRenameProject'
        QtMocHelpers::SlotData<void()>(70, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmDeleteProject'
        QtMocHelpers::SlotData<void()>(71, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmAddWatchFolder'
        QtMocHelpers::SlotData<void()>(72, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmViewModeToggled'
        QtMocHelpers::SlotData<void()>(73, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmThumbnailSizeChanged'
        QtMocHelpers::SlotData<void(int)>(74, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 27 },
        }}),
        // Slot 'onPmToggleShowAllVersions'
        QtMocHelpers::SlotData<void(bool)>(75, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onPmVersionSelected'
        QtMocHelpers::SlotData<void(qint64, const QString &)>(76, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::LongLong, 54 }, { QMetaType::QString, 77 },
        }}),
        // Slot 'onPmVersionDropdownRequested'
        QtMocHelpers::SlotData<void(const QModelIndex &, const QPoint &)>(78, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { 0x80000000 | 8, 79 },
        }}),
        // Slot 'onPmRefresh'
        QtMocHelpers::SlotData<void()>(80, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'generateProjectThumbnails'
        QtMocHelpers::SlotData<void(int, bool)>(81, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 82 }, { QMetaType::Bool, 83 },
        }}),
        // Slot 'onPmMarkNotificationsRead'
        QtMocHelpers::SlotData<void()>(84, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmShowNotifications'
        QtMocHelpers::SlotData<void()>(85, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmNewFilesDetected'
        QtMocHelpers::SlotData<void(int, const QStringList &)>(86, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 82 }, { QMetaType::QStringList, 87 },
        }}),
        // Slot 'onPmFilesRemoved'
        QtMocHelpers::SlotData<void(int, const QStringList &)>(88, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 82 }, { QMetaType::QStringList, 89 },
        }}),
        // Slot 'onPmOpenOverlay'
        QtMocHelpers::SlotData<void()>(90, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'changePmPreview'
        QtMocHelpers::SlotData<void(int)>(91, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Slot 'onPmNavigateBack'
        QtMocHelpers::SlotData<void()>(92, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmNavigateUp'
        QtMocHelpers::SlotData<void()>(93, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmCopy'
        QtMocHelpers::SlotData<void()>(94, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmCut'
        QtMocHelpers::SlotData<void()>(95, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmPaste'
        QtMocHelpers::SlotData<void()>(96, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmDelete'
        QtMocHelpers::SlotData<void()>(97, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmRename'
        QtMocHelpers::SlotData<void()>(98, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmNewFolder'
        QtMocHelpers::SlotData<void()>(99, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmOpenExternal'
        QtMocHelpers::SlotData<void()>(100, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onPmTogglePreview'
        QtMocHelpers::SlotData<void(bool)>(101, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onPmGroupSequencesToggled'
        QtMocHelpers::SlotData<void(bool)>(102, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onShowUserGuide'
        QtMocHelpers::SlotData<void()>(103, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAboutKAssetManager'
        QtMocHelpers::SlotData<void()>(104, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetNavigateBack'
        QtMocHelpers::SlotData<void()>(105, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetNavigateUp'
        QtMocHelpers::SlotData<void()>(106, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetNewFolder'
        QtMocHelpers::SlotData<void()>(107, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetGroupSequencesToggled'
        QtMocHelpers::SlotData<void(bool)>(108, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onAssetFoldersModelAboutToReset'
        QtMocHelpers::SlotData<void()>(109, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAssetFoldersModelReset'
        QtMocHelpers::SlotData<void()>(110, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmTreeCurrentChanged'
        QtMocHelpers::SlotData<void(const QModelIndex &, const QModelIndex &)>(111, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 32 }, { 0x80000000 | 3, 112 },
        }}),
        // Slot 'onFmTreeActivated'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(113, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onFmTreeContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(114, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onFmItemDoubleClicked'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(115, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Slot 'onFmViewModeToggled'
        QtMocHelpers::SlotData<void()>(116, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmThumbnailSizeChanged'
        QtMocHelpers::SlotData<void(int)>(117, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 27 },
        }}),
        // Slot 'onAddSelectionToAssetLibrary'
        QtMocHelpers::SlotData<void()>(118, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onAddTreeSelectionToAssetLibrary'
        QtMocHelpers::SlotData<void()>(119, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmAddToFavorites'
        QtMocHelpers::SlotData<void()>(120, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmRemoveFavorite'
        QtMocHelpers::SlotData<void()>(121, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmFavoriteActivated'
        QtMocHelpers::SlotData<void(QListWidgetItem *)>(122, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 123, 124 },
        }}),
        // Slot 'onFmNavigateBack'
        QtMocHelpers::SlotData<void()>(125, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmNavigateUp'
        QtMocHelpers::SlotData<void()>(126, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmCopy'
        QtMocHelpers::SlotData<void()>(127, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmCut'
        QtMocHelpers::SlotData<void()>(128, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmPaste'
        QtMocHelpers::SlotData<void()>(129, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmDelete'
        QtMocHelpers::SlotData<void()>(130, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmDeletePermanent'
        QtMocHelpers::SlotData<void()>(131, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmRename'
        QtMocHelpers::SlotData<void()>(132, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmBulkRename'
        QtMocHelpers::SlotData<void()>(133, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmNewFolder'
        QtMocHelpers::SlotData<void()>(134, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmCreateFolderWithSelected'
        QtMocHelpers::SlotData<void()>(135, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmShowContextMenu'
        QtMocHelpers::SlotData<void(const QPoint &)>(136, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 8, 9 },
        }}),
        // Slot 'onFmBackToParent'
        QtMocHelpers::SlotData<void()>(137, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmRefresh'
        QtMocHelpers::SlotData<void()>(138, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmLightRefresh'
        QtMocHelpers::SlotData<void()>(139, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmGroupSequencesToggled'
        QtMocHelpers::SlotData<void(bool)>(140, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onFmHideFoldersToggled'
        QtMocHelpers::SlotData<void(bool)>(141, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onFmToggleSecondPane'
        QtMocHelpers::SlotData<void(bool)>(142, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onFmSyncNavToggled'
        QtMocHelpers::SlotData<void(bool)>(143, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Bool, 48 },
        }}),
        // Slot 'onEverythingSearchAssetManager'
        QtMocHelpers::SlotData<void()>(144, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEverythingSearchFileManager'
        QtMocHelpers::SlotData<void()>(145, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEverythingImportRequested'
        QtMocHelpers::SlotData<void(const QStringList &)>(146, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QStringList, 147 },
        }}),
        // Slot 'onFmSelectionChanged'
        QtMocHelpers::SlotData<void()>(148, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmTogglePreview'
        QtMocHelpers::SlotData<void()>(149, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmOpenOverlay'
        QtMocHelpers::SlotData<void()>(150, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'changeFmPreview'
        QtMocHelpers::SlotData<void(int)>(151, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Slot 'applyTheme'
        QtMocHelpers::SlotData<void()>(152, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onFmTreeChildrenFetched'
        QtMocHelpers::SlotData<void(const QModelIndex &)>(153, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { 0x80000000 | 3, 154 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MainWindow, qt_meta_tag_ZN10MainWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN10MainWindowE_t>.metaTypes,
    nullptr
} };

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MainWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->onFolderSelected((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 1: _t->onAssetSelectionChanged(); break;
        case 2: _t->onAssetDoubleClicked((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 3: _t->onAssetContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 4: _t->onFolderContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 5: _t->onEmptySpaceContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 6: _t->showPreview((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->closePreview(); break;
        case 8: _t->changePreview((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->applyFilters(); break;
        case 10: _t->clearFilters(); break;
        case 11: _t->onSearchTextChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 12: _t->onCreateTag(); break;
        case 13: _t->onApplyTags(); break;
        case 14: _t->onFilterByTags(); break;
        case 15: _t->onTagContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 16: _t->updateTagButtonStates(); break;
        case 17: _t->onOpenSettings(); break;
        case 18: _t->onThumbnailSizeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 19: _t->onViewModeChanged(); break;
        case 20: _t->importFiles((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 21: _t->onImportProgress((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        case 22: _t->onImportFileChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 23: _t->onImportFolderChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 24: _t->onImportComplete(); break;
        case 25: _t->onRatingChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 26: _t->onPrefetchLivePreviewsForFolder(); break;
        case 27: _t->onRefreshLivePreviewsForFolder(); break;
        case 28: _t->onPrefetchLivePreviewsRecursive(); break;
        case 29: _t->onRefreshLivePreviewsRecursive(); break;
        case 30: _t->onAddProjectFolder(); break;
        case 31: _t->onRefreshAssets(); break;
        case 32: _t->onLockToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 33: _t->onProjectFolderChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 34: _t->onRevertSelectedVersion(); break;
        case 35: _t->onAssetVersionsChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 36: _t->onToggleLogViewer(); break;
        case 37: _t->showDatabaseHealthDialog(); break;
        case 38: _t->performStartupHealthCheck(); break;
        case 39: _t->onTabChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 40: _t->onPmProjectSelected((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 41: _t->onPmFolderSelected((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 42: _t->onPmAssetSelectionChanged(); break;
        case 43: _t->onPmAssetDoubleClicked((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 44: _t->onPmAssetContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 45: _t->onPmProjectContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 46: _t->onPmFolderContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 47: _t->onPmCreateProject(); break;
        case 48: _t->pmImportToProject((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 49: _t->onPmRenameProject(); break;
        case 50: _t->onPmDeleteProject(); break;
        case 51: _t->onPmAddWatchFolder(); break;
        case 52: _t->onPmViewModeToggled(); break;
        case 53: _t->onPmThumbnailSizeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 54: _t->onPmToggleShowAllVersions((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 55: _t->onPmVersionSelected((*reinterpret_cast<std::add_pointer_t<qint64>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 56: _t->onPmVersionDropdownRequested((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[2]))); break;
        case 57: _t->onPmRefresh(); break;
        case 58: _t->generateProjectThumbnails((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 59: _t->onPmMarkNotificationsRead(); break;
        case 60: _t->onPmShowNotifications(); break;
        case 61: _t->onPmNewFilesDetected((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 62: _t->onPmFilesRemoved((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[2]))); break;
        case 63: _t->onPmOpenOverlay(); break;
        case 64: _t->changePmPreview((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 65: _t->onPmNavigateBack(); break;
        case 66: _t->onPmNavigateUp(); break;
        case 67: _t->onPmCopy(); break;
        case 68: _t->onPmCut(); break;
        case 69: _t->onPmPaste(); break;
        case 70: _t->onPmDelete(); break;
        case 71: _t->onPmRename(); break;
        case 72: _t->onPmNewFolder(); break;
        case 73: _t->onPmOpenExternal(); break;
        case 74: _t->onPmTogglePreview((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 75: _t->onPmGroupSequencesToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 76: _t->onShowUserGuide(); break;
        case 77: _t->onAboutKAssetManager(); break;
        case 78: _t->onAssetNavigateBack(); break;
        case 79: _t->onAssetNavigateUp(); break;
        case 80: _t->onAssetNewFolder(); break;
        case 81: _t->onAssetGroupSequencesToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 82: _t->onAssetFoldersModelAboutToReset(); break;
        case 83: _t->onAssetFoldersModelReset(); break;
        case 84: _t->onFmTreeCurrentChanged((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[2]))); break;
        case 85: _t->onFmTreeActivated((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 86: _t->onFmTreeContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 87: _t->onFmItemDoubleClicked((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        case 88: _t->onFmViewModeToggled(); break;
        case 89: _t->onFmThumbnailSizeChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 90: _t->onAddSelectionToAssetLibrary(); break;
        case 91: _t->onAddTreeSelectionToAssetLibrary(); break;
        case 92: _t->onFmAddToFavorites(); break;
        case 93: _t->onFmRemoveFavorite(); break;
        case 94: _t->onFmFavoriteActivated((*reinterpret_cast<std::add_pointer_t<QListWidgetItem*>>(_a[1]))); break;
        case 95: _t->onFmNavigateBack(); break;
        case 96: _t->onFmNavigateUp(); break;
        case 97: _t->onFmCopy(); break;
        case 98: _t->onFmCut(); break;
        case 99: _t->onFmPaste(); break;
        case 100: _t->onFmDelete(); break;
        case 101: _t->onFmDeletePermanent(); break;
        case 102: _t->onFmRename(); break;
        case 103: _t->onFmBulkRename(); break;
        case 104: _t->onFmNewFolder(); break;
        case 105: _t->onFmCreateFolderWithSelected(); break;
        case 106: _t->onFmShowContextMenu((*reinterpret_cast<std::add_pointer_t<QPoint>>(_a[1]))); break;
        case 107: _t->onFmBackToParent(); break;
        case 108: _t->onFmRefresh(); break;
        case 109: _t->onFmLightRefresh(); break;
        case 110: _t->onFmGroupSequencesToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 111: _t->onFmHideFoldersToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 112: _t->onFmToggleSecondPane((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 113: _t->onFmSyncNavToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 114: _t->onEverythingSearchAssetManager(); break;
        case 115: _t->onEverythingSearchFileManager(); break;
        case 116: _t->onEverythingImportRequested((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 117: _t->onFmSelectionChanged(); break;
        case 118: _t->onFmTogglePreview(); break;
        case 119: _t->onFmOpenOverlay(); break;
        case 120: _t->changeFmPreview((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 121: _t->applyTheme(); break;
        case 122: _t->onFmTreeChildrenFetched((*reinterpret_cast<std::add_pointer_t<QModelIndex>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN10MainWindowE_t>.strings))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 123)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 123;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 123)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 123;
    }
    return _id;
}
QT_WARNING_POP
