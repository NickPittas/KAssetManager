---
status: diagnosed
trigger: "Investigate the remaining Phase 06 blocker around Project Manager watcher/import mismatch. Focus: Initial Project Manager import/scan appears to ingest more files than the watcher later tracks. We need the concrete runtime path, exact mismatch, and minimal fix strategy."
created: 2026-04-14T00:00:00Z
updated: 2026-04-14T00:32:00Z
---

## Current Focus

hypothesis: Confirmed: Project Manager initial import ingests all regular files, while watcher cache/rescan/event processing only tracks a hard-coded extension subset; incremental ProjectDB updates therefore ignore unsupported files.
test: Compared importFolderContents(), watchProject()/buildDirectoryCache()/scanSingleDirectory()/processChangedDirectories()/onProcessChanges(), and MainWindow watcher signal handlers.
expecting: N/A
next_action: Return diagnosis with minimal fix strategy and validation checklist.

## Symptoms

expected: Project Manager initial import/scan and later watcher/rescan should operate on the same file set.
actual: Initial import/scan appears to ingest more files than the watcher later tracks.
errors: Remaining Phase 06 blocker; watcher/import mismatch on Linux.
reproduction: Import/create project, observe imported files, then compare later watcher-monitored updates/rescan behavior.
started: Phase 06 verification.

## Eliminated

## Evidence

- timestamp: 2026-04-14T00:10:00Z
  checked: Planning docs and repository codemap
  found: Phase 06 research and plan already identify initial-import vs watcher-scope disagreement as an active blocker; knowledge base file does not exist.
  implication: Investigation should verify the exact runtime mismatch directly in code rather than assume the earlier summary is complete.

- timestamp: 2026-04-14T00:18:00Z
  checked: Symbol search across native/qt6/src
  found: MainWindow PM flow calls watcher watch/rescan from setupProjectManagerUi, pmImportToProject, and onPmRefresh; watcher logic is concentrated in project_manager_watcher.cpp; importer/importFolderContents and ProjectDB::ensureFolderForPath/removeAssetsByPath/updateAssetPath are the main ingest/reconciliation paths.
  implication: A full read of those functions should be sufficient to trace the concrete runtime mismatch end-to-end.

- timestamp: 2026-04-14T00:32:00Z
  checked: MainWindow::pmImportToProject and Importer::importFolderContents
  found: New project flow starts watcher first via pmWatcher->watchProject(projectId, watchPath), then imports via importFolderContents(watchPath, rootFolderId). importFolderContents explicitly imports ALL files under the tree (comment: "PM should work like a file manager") and inserts every file into ProjectDB, with sequence detection only affecting grouping. 
  implication: Initial ProjectDB state intentionally contains every regular file in the project tree, not only media/project extensions.

- timestamp: 2026-04-14T00:32:00Z
  checked: ProjectManagerWatcher::scanSingleDirectory, buildDirectoryCache, processChangedDirectories, onProcessChanges
  found: Watcher cache and both incremental/full-rescan comparisons are built only from scanSingleDirectory(), which filters to s_supportedExtensions = {jpg,jpeg,png,tif,tiff,exr,dpx,bmp,gif,mov,mp4,avi,mkv,mxf,r3d,aep,aepx,nk}. Unsupported files never enter m_dirFiles, oldFiles, or newFiles.
  implication: Watcher change detection only reasons about that subset, regardless of what the importer previously inserted into ProjectDB.

- timestamp: 2026-04-14T00:32:00Z
  checked: MainWindow::onPmRefresh, onPmNewFilesDetected, onPmFilesRemoved, and ProjectDB reconciliation helpers
  found: Manual refresh only triggers pmWatcher->rescan(projectId); full rescan emits added/removed lists derived from the watcher subset. MainWindow then updates ProjectDB only through onPmNewFilesDetected -> insertAssetMetadataFast and onPmFilesRemoved -> removeAssetsByPath. No alternate refresh reconciliation path exists here for unsupported files.
  implication: Unsupported files imported initially can become permanently stale in ProjectDB after external delete/rename, and unsupported files added later never appear, even after Refresh.

- timestamp: 2026-04-14T00:32:00Z
  checked: Linux event path through QFileSystemWatcher directoryChanged
  found: On Linux, directory changes are received at directory granularity, then ProjectManagerWatcher rescans the changed directory and applies its own extension filter before emitting signals.
  implication: The mismatch is in application logic, not Linux event delivery; Linux still exposes it because normal add/remove/rename events reach the watcher, but unsupported files are discarded during rescans.

## Resolution

root_cause: 
root_cause: Project Manager has two different file eligibility rules. Initial import (Importer::importFolderContents) imports every regular file under the project tree, but watcher startup, incremental directory-change processing, and manual rescan (ProjectManagerWatcher::scanSingleDirectory -> buildDirectoryCache/processChangedDirectories/onProcessChanges) only track a hard-coded extension allowlist. MainWindow updates ProjectDB only from watcher-added/watcher-removed signals, so unsupported files are never reconciled after initial import.
fix: Minimal fix strategy is to make Project Manager use one shared tracking rule for both initial import and watcher/rescan paths. To preserve current PM semantics, the smallest behavior-preserving direction is to remove the watcher-only allowlist and have scanSingleDirectory/watcher cache include all regular files that importFolderContents can ingest.
verification: Diagnosis only; no code changes applied.
files_changed: []
