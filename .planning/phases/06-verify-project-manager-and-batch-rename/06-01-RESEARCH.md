# Phase 06: Verify Project Manager And Batch Rename - Research

**Researched:** 2026-04-14  
**Phase objective:** Validate Project Manager and batch rename workflows on Linux and only fix Linux-specific issues that are actually present. [VERIFIED: .planning/ROADMAP.md, .planning/REQUIREMENTS.md, .planning/STATE.md]

## Summary

Phase 06 should start from the real runtime pipeline already in the code, not from the File Manager assumptions. [VERIFIED: AGENTS.md, codemap.md, native/qt6/src/codemap.md] The Project Manager already has Linux-facing browse/open/reveal/delete/copy/cut/paste/watch/import code paths, but several important behaviors are either rooted at the project watch folder instead of the selected virtual folder, or are not wired into Project Manager at all. [VERIFIED: native/qt6/src/mainwindow.cpp, native/qt6/src/project_manager_watcher.cpp, native/qt6/src/project_db.cpp]

The highest-risk blockers are not Wayland rendering issues. [VERIFIED: codebase read] They are workflow mismatches: Project Manager has no batch rename entry point, Project Manager paste/new-folder target the project root watch path instead of the selected folder, manual refresh does not force a watcher rescan, and watcher incremental imports do not match initial import scope. [VERIFIED: native/qt6/src/mainwindow.cpp, native/qt6/src/project_manager_watcher.cpp, native/qt6/src/importer.cpp]

**Primary recommendation:** First implement and validate one narrow slice: add Project Manager batch rename wiring for selected assets in the active folder view, then fix selected-folder destination handling for paste/new-folder, and only after that validate watcher/refresh behavior on Linux. [VERIFIED: native/qt6/src/mainwindow.cpp, native/qt6/src/bulk_rename_dialog.cpp]

## Project Constraints (from AGENTS.md)

- Read codemaps before working and trace the real runtime/build path before deciding anything. [VERIFIED: AGENTS.md]
- Base findings on present code and observed evidence, not speculation. [VERIFIED: AGENTS.md]
- Fix gatekeeping blockers before downstream polish. [VERIFIED: AGENTS.md]
- Prefer the smallest justified change and re-check all affected paths after each fix. [VERIFIED: AGENTS.md]
- Verify results with concrete evidence; do not present speculative fixes as complete. [VERIFIED: AGENTS.md]
- Only write inside the repository root. [VERIFIED: AGENTS.md]

## Traced Runtime Paths

### 1) Project creation and initial import

- `MainWindow::setupProjectManagerUi()` initializes `ProjectDB` at `QCoreApplication::applicationDirPath() + "/data/projects.db"`, creates the Project Manager views, creates `ProjectManagerWatcher`, and starts watching all existing projects. [VERIFIED: native/qt6/src/mainwindow.cpp:3910-4628]
- New project flow is `onPmCreateProject()` → `pmImportToProject(name, watchPath)` → `ProjectsModel::createProject()` → `ProjectDB::createProject()` → `pmWatcher->watchProject(projectId, watchPath)` → `Importer::importFolderContents(watchPath, rootFolderId)`. [VERIFIED: native/qt6/src/mainwindow.cpp:5095-5167, native/qt6/src/projects_model.cpp:65-71, native/qt6/src/project_db.cpp:288-336, native/qt6/src/importer.cpp:219-319]
- Initial import imports all files found under the dropped/selected directory, then uses sequence detection per directory before writing into `ProjectDB`. [VERIFIED: native/qt6/src/importer.cpp:256-319]

### 2) Browse, selection, preview, and reveal/open

- Project selection is `onPmProjectSelected()`; it loads the folder tree, clears folder history, and sets the assets model to “all assets in project”. [VERIFIED: native/qt6/src/mainwindow.cpp:4631-4662]
- Folder selection is `onPmFolderSelected()`; it pushes folder history and filters `ProjectAssetsModel` to the selected folder id. [VERIFIED: native/qt6/src/mainwindow.cpp:4664-4685]
- Asset selection is `onPmAssetSelectionChanged()`; it updates preview and info from the first selected item in the active PM view. [VERIFIED: native/qt6/src/mainwindow.cpp:4687-4704]
- Double-click is split: folder rows navigate deeper; project files try configured external executables and otherwise fall back to `QDesktopServices::openUrl`; media files open the preview overlay. [VERIFIED: native/qt6/src/mainwindow.cpp:4706-4861]
- “Show in Explorer” from the PM context menu calls `DragUtils::showInExplorer(path)`. On Linux that first tries `org.freedesktop.FileManager1.ShowItems` over DBus and falls back to opening the containing directory with `QDesktopServices`. [VERIFIED: native/qt6/src/mainwindow.cpp:4931-4963, native/qt6/src/drag_utils.cpp:87-124]

### 3) Project Manager file actions and refresh

- PM copy/cut only cache selected file paths in `pmClipboard`. [VERIFIED: native/qt6/src/mainwindow.cpp:6415-6431]
- PM paste resolves destination from the project watch path, not from the currently selected PM folder, then enqueues copy/move in `FileOpsQueue`; cut also removes DB rows before queueing the move. [VERIFIED: native/qt6/src/mainwindow.cpp:6433-6472]
- PM delete removes DB rows immediately via `ProjectDB::removeAssetsByPath()` and then queues trash/delete in `FileOpsQueue`; non-Windows delete uses `QFile::moveToTrash`. [VERIFIED: native/qt6/src/mainwindow.cpp:6474-6495, native/qt6/src/file_ops.cpp:339-372]
- PM single rename is local-only: `onPmRename()` calls `QDir::rename()` on the filesystem and then `ProjectDB::updateAssetPath(oldPath, newPath)`. [VERIFIED: native/qt6/src/mainwindow.cpp:6497-6522, native/qt6/src/project_db.cpp:834-846]
- PM new folder also targets the project watch path root and then just calls `onPmRefresh()`. [VERIFIED: native/qt6/src/mainwindow.cpp:6524-6554]
- PM refresh only reloads project/asset models; it does not call `pmWatcher->rescan(projectId)`. [VERIFIED: native/qt6/src/mainwindow.cpp:5391-5399, native/qt6/src/project_manager_watcher.cpp:145-149]

### 4) Drag/drop relevance

- Top-level app drop handling treats a single dropped folder on the Project Manager tab as “create/import project”; dropped files are rejected for PM. [VERIFIED: native/qt6/src/mainwindow.cpp:8501-8544]
- PM asset views are drag-enabled through `ProjectAssetsModel` / `ProjectSequenceGroupingProxyModel` URL mime data, but there is no matching PM drop handling for internal folder placement, and `ProjectFoldersModel::flags()` does not expose drop-enabled items. [VERIFIED: native/qt6/src/project_assets_model.cpp:122-157, native/qt6/src/project_sequence_grouping_proxy_model.cpp:120-145, native/qt6/src/project_folders_model.cpp:210-214, native/qt6/src/mainwindow.cpp]

### 5) Batch rename integration points

- `BulkRenameDialog` exists in two modes only: Asset Manager (`QVector<int> assetIds`) and File Manager (`QStringList filePaths`). [VERIFIED: native/qt6/src/bulk_rename_dialog.h:27-58]
- File Manager launches it from `onFmBulkRename()` with selected filesystem paths. [VERIFIED: native/qt6/src/mainwindow.cpp:2881-2900]
- Asset Manager launches it from the asset context menu with selected asset ids. [VERIFIED: native/qt6/src/mainwindow.cpp:7033-7193]
- Project Manager does not launch `BulkRenameDialog` anywhere. [VERIFIED: native/qt6/src/mainwindow.cpp, native/qt6/src/bulk_rename_dialog.cpp, native/qt6/src/bulk_rename_dialog.h]

## Linux-Specific Risk Areas

- PM reveal/show-in-folder is Linux-aware through DBus `org.freedesktop.FileManager1.ShowItems`, but the UI label still says “Show in Explorer”, so Linux validation should verify behavior rather than name parity. [VERIFIED: native/qt6/src/mainwindow.cpp:4931-4963, native/qt6/src/drag_utils.cpp:96-120]
- PM data storage uses `applicationDirPath()/data/projects.db`, not a user-writable platform data path. Linux validation must prove this is writable in the actual runtime target before Phase 07/08 packaging work. [VERIFIED: native/qt6/src/mainwindow.cpp:3914-3921, native/qt6/src/main.cpp:140-145]
- Path normalization for PM import/folder mapping lowercases paths (`toLower()`), which is a Linux-sensitive assumption because Linux filesystems are typically case-sensitive. [VERIFIED: native/qt6/src/importer.cpp:231-238, native/qt6/src/project_db.cpp:732-750]
- PM incremental watch updates only ingest files whose extensions are in the watcher allowlist, while initial PM import reads all files. This mismatch is likely to surface during Linux validation as “new file not appearing after initial project import”. [VERIFIED: native/qt6/src/project_manager_watcher.cpp:11-19,161-173,327-350, native/qt6/src/importer.cpp:256-319]
- PM delete/move flows depend on `QFile::moveToTrash` and non-Windows file operation fallbacks; Linux validation should explicitly cover trash availability, same-folder no-op, and cross-directory move/copy conflict naming. [VERIFIED: native/qt6/src/file_ops.cpp:46-60,274-372]

## Likely Blockers

| Blocker | Evidence | Impact |
|---|---|---|
| Project Manager has no batch rename entry point. [VERIFIED: codebase read] | `BulkRenameDialog` is only launched from File Manager and Asset Manager, never from PM. [VERIFIED: native/qt6/src/mainwindow.cpp:2881-2900,7000-7193] | Phase requirement `FEDORA43-PROJECTS` cannot be closed if PM batch rename is expected to be part of PM validation. [VERIFIED: .planning/REQUIREMENTS.md] |
| PM paste/new-folder target the project watch root, not the selected PM folder. [VERIFIED: codebase read] | `onPmPaste()` and `onPmNewFolder()` derive `destDir` from `Project.watchPath`. [VERIFIED: native/qt6/src/mainwindow.cpp:6433-6472,6524-6554] | Folder-scoped workflows will behave incorrectly even if Linux file ops themselves work. [VERIFIED: codebase read] |
| PM manual refresh does not force watcher/database reconciliation. [VERIFIED: codebase read] | `onPmRefresh()` only reloads models; watcher has `rescan()` but PM refresh never calls it. [VERIFIED: native/qt6/src/mainwindow.cpp:5391-5399, native/qt6/src/project_manager_watcher.cpp:145-149] | Missed filesystem events on Linux can remain invisible until some other watcher event happens. [VERIFIED: codebase read] |
| Initial import and watcher update scope disagree. [VERIFIED: codebase read] | Importer imports all files; watcher only tracks a limited extension set. [VERIFIED: native/qt6/src/importer.cpp:256-319, native/qt6/src/project_manager_watcher.cpp:11-19,161-173] | PM contents can diverge after project creation, producing hard-to-debug Linux validation failures. [VERIFIED: codebase read] |
| PM path handling lowercases paths during folder mapping. [VERIFIED: codebase read] | Both importer and `ensureFolderForPath()` normalize with `toLower()`. [VERIFIED: native/qt6/src/importer.cpp:231-238, native/qt6/src/project_db.cpp:739-750] | Case-distinct Linux paths can collapse into the same virtual-folder mapping. [VERIFIED: codebase read] |

## Recommended First Implementation Slice

1. Add a Project Manager context-menu and/or shortcut entry that launches `BulkRenameDialog` for multiple selected PM assets using PM asset ids, mirroring Asset Manager wiring. [VERIFIED: native/qt6/src/mainwindow.cpp:4863-5036,7000-7193, native/qt6/src/bulk_rename_dialog.h]
2. Fix PM destination resolution so paste and new-folder use the selected PM folder’s real filesystem path, not always the project root watch path. [VERIFIED: native/qt6/src/mainwindow.cpp:6433-6472,6524-6554, native/qt6/src/project_db.cpp:726-759]
3. Make PM refresh trigger watcher/database reconciliation for the active project, then validate add/remove/rename flows on Linux with real filesystem changes. [VERIFIED: native/qt6/src/mainwindow.cpp:5391-5399, native/qt6/src/project_manager_watcher.cpp:145-149]
4. After that slice is green, run targeted Linux manual validation for: create project by picker, create project by folder drop, select folder, preview media, reveal in file manager, single rename, batch rename, paste/cut/delete, refresh after out-of-app filesystem changes, and notification navigation. [VERIFIED: native/qt6/src/mainwindow.cpp, native/qt6/src/drag_utils.cpp]

## Sources

- `AGENTS.md` [VERIFIED: read]
- `codemap.md` [VERIFIED: read]
- `.planning/ROADMAP.md` [VERIFIED: read]
- `.planning/STATE.md` [VERIFIED: read]
- `.planning/REQUIREMENTS.md` [VERIFIED: read]
- `native/qt6/src/mainwindow.cpp` [VERIFIED: read]
- `native/qt6/src/project_manager_watcher.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/project_db.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/importer.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/bulk_rename_dialog.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/drag_utils.cpp` [VERIFIED: read]
- `native/qt6/src/project_assets_model.cpp` [VERIFIED: read]
- `native/qt6/src/project_sequence_grouping_proxy_model.cpp` [VERIFIED: read]
- `native/qt6/src/project_folders_model.cpp` [VERIFIED: read]
