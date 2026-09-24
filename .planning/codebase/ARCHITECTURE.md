# Architecture

**Analysis Date:** 2026-04-11

## Pattern Overview

**Overall:** Qt Widgets desktop application with a central composition root, Qt model/view presentation, singleton service managers, and SQLite-backed persistence.

**Key Characteristics:**
- `native/qt6/src/main.cpp` bootstraps global runtime concerns, then hands control to `native/qt6/src/mainwindow.cpp`.
- `native/qt6/src/mainwindow.{h,cpp}` acts as the orchestration shell for Asset Manager, File Manager, and Project Manager.
- Data access is split between two SQLite services, `native/qt6/src/db.{h,cpp}` and `native/qt6/src/project_db.{h,cpp}`, with shared import behavior through `native/qt6/src/i_asset_database.h` and `native/qt6/src/importer.{h,cpp}`.

## Layers

**Application Bootstrap:**
- Purpose: Initialize Qt runtime, theme, logging, persistent storage paths, databases, and the main window.
- Location: `native/qt6/src/main.cpp`
- Contains: `QApplication` startup, high-DPI/OpenGL setup, tlRender initialization, crash logging, database migration, message handler installation.
- Depends on: `native/qt6/src/mainwindow.h`, `native/qt6/src/db.h`, `native/qt6/src/log_manager.h`, `native/qt6/src/progress_manager.h`, `native/qt6/src/theme_manager.h`, `native/qt6/src/platform_session.h`, `native/qt6/src/media/tlrender_player.h`.
- Used by: CMake target `kassetmanagerqt` in `native/qt6/CMakeLists.txt`.

**UI Shell and Screens:**
- Purpose: Build and coordinate the top-level desktop UI.
- Location: `native/qt6/src/mainwindow.{h,cpp}` and `native/qt6/src/file_manager_pane.{h,cpp}`
- Contains: Tab shell, splitters, menus, toolbars, dock widgets, overlays, shortcuts, preview panels, per-tab interaction handlers.
- Depends on: Qt models, dialogs, delegates, service singletons, file operations, media widgets.
- Used by: `native/qt6/src/main.cpp`.

**Presentation Models:**
- Purpose: Expose database rows and filesystem entries to Qt views.
- Location: `native/qt6/src/assets_model.{h,cpp}`, `native/qt6/src/virtual_folders.{h,cpp}`, `native/qt6/src/projects_model.{h,cpp}`, `native/qt6/src/project_assets_model.{h,cpp}`, `native/qt6/src/project_folders_model.{h,cpp}`, `native/qt6/src/everything_folder_model.{h,cpp}`, `native/qt6/src/assets_table_model.h`, `native/qt6/src/tags_model.h`
- Contains: `QAbstractListModel`, `QAbstractItemModel`, and proxy-backed view state.
- Depends on: `native/qt6/src/db.h`, `native/qt6/src/project_db.h`, `native/qt6/src/sequence_detector.h`, Qt SQL and filesystem APIs.
- Used by: `native/qt6/src/mainwindow.cpp`, `native/qt6/src/file_manager_pane.cpp`, delegates, views.

**Proxy and View Behavior:**
- Purpose: Transform raw model data into grouped, filtered, scrub-capable view data.
- Location: `native/qt6/src/sequence_grouping_proxy_model.{h,cpp}`, `native/qt6/src/asset_sequence_grouping_proxy_model.{h,cpp}`, `native/qt6/src/project_sequence_grouping_proxy_model.{h,cpp}`, `native/qt6/src/grid_scrub.{h,cpp}`, `native/qt6/src/asset_item_delegate.{h,cpp}`, `native/qt6/src/fm_item_delegate.{h,cpp}`, `native/qt6/src/project_item_delegate.{h,cpp}`, `native/qt6/src/asset_grid_view.{h,cpp}`, `native/qt6/src/fm_views_ex.{h,cpp}`
- Contains: Sequence grouping, custom painting, hover scrubbing, custom list/grid interactions.
- Depends on: Source models, `native/qt6/src/live_preview_manager.h`, `native/qt6/src/sequence_detector.h`, Qt view classes.
- Used by: Asset Manager, File Manager, and Project Manager view stacks.

**Persistence and Database Services:**
- Purpose: Own application and project data stores.
- Location: `native/qt6/src/db.{h,cpp}`, `native/qt6/src/project_db.{h,cpp}`, `native/qt6/src/i_asset_database.h`
- Contains: Schema migration, CRUD for folders/assets/tags/versions/projects/notifications, prepared-statement helpers, change signals.
- Depends on: `QSqlDatabase`, `QSqlQuery`, filesystem metadata.
- Used by: Models, importers, watchers, dialogs, health checks, main window actions.

**Background Work and System Services:**
- Purpose: Perform long-running or reusable operations off the immediate UI path.
- Location: `native/qt6/src/importer.{h,cpp}`, `native/qt6/src/project_import_worker.{h,cpp}`, `native/qt6/src/file_ops.{h,cpp}`, `native/qt6/src/live_preview_manager.{h,cpp}`, `native/qt6/src/project_folder_watcher.{h,cpp}`, `native/qt6/src/project_manager_watcher.{h,cpp}`, `native/qt6/src/database_health_agent.{h,cpp}`, `native/qt6/src/context_preserver.{h,cpp}`, `native/qt6/src/progress_manager.{h,cpp}`, `native/qt6/src/log_manager.{h,cpp}`
- Contains: Bulk import, threaded project import, queued file operations, frame decode cache, filesystem watchers, health audits, state persistence, progress aggregation, logging.
- Depends on: Database services, QtConcurrent/QThreadPool/QFileSystemWatcher/QSettings.
- Used by: `native/qt6/src/mainwindow.cpp`, `native/qt6/src/file_manager_pane.cpp`, preview and import flows.

**Media and Preview Integration:**
- Purpose: Render stills, video, sequences, and document previews.
- Location: `native/qt6/src/media/tlrender_player.{h,cpp}`, `native/qt6/src/media/tlrender_viewport.{h,cpp}`, `native/qt6/src/media/tlrender_widget.{h,cpp}`, `native/qt6/src/oiio_image_loader.{h,cpp}`, `native/qt6/src/preview_overlay.{h,cpp}`, `native/qt6/src/image_preview_overlay.{h,cpp}`, `native/qt6/src/video_metadata.{h,cpp}`, `native/qt6/src/office_preview.{h,cpp}`, `native/qt6/src/thumbnail_cache_manager.{h,cpp}`, `native/qt6/src/thumbnail_generator_dialog.{h,cpp}`, `native/qt6/src/thumbnail_generator_worker.{h,cpp}`
- Contains: tlRender playback, OpenImageIO still loading, overlay navigation, metadata extraction, thumbnail generation.
- Depends on: tlRender, Qt multimedia/OpenGL/PDF/SVG, filesystem paths.
- Used by: Delegates, File Manager, Project Manager, Asset Manager preview overlay.

## Data Flow

**Application Startup Flow:**

1. `native/qt6/src/main.cpp` configures DPI, graphics attributes, Wayland behavior, and tlRender initialization.
2. `native/qt6/src/main.cpp` installs the centralized Qt message handler from `native/qt6/src/log_manager.h`, creates the persistent app-data directory, migrates the legacy database if needed, and initializes `DB::instance()`.
3. `native/qt6/src/main.cpp` constructs `MainWindow`, and `native/qt6/src/mainwindow.cpp` builds tabs, models, splitters, watchers, and restores session state through `native/qt6/src/context_preserver.{h,cpp}` and `QSettings`.

**Asset Manager Navigation Flow:**

1. `native/qt6/src/virtual_folders.{h,cpp}` loads the virtual folder tree and binds it to `folderTreeView` in `native/qt6/src/mainwindow.cpp`.
2. Clicking a folder triggers `MainWindow::onFolderSelected()` in `native/qt6/src/mainwindow.cpp`, which debounces into `MainWindow::navigateToFolder()`.
3. `MainWindow::navigateToFolder()` saves current per-folder UI state through `native/qt6/src/context_preserver.{h,cpp}`, cancels pending preview work via `native/qt6/src/live_preview_manager.{h,cpp}`, updates `native/qt6/src/assets_model.{h,cpp}`, then restores saved view/filter state if present.

**Asset Import Flow:**

1. Asset imports start from drag/drop or File Manager actions in `native/qt6/src/mainwindow.cpp` and route into `MainWindow::importToAssetLibrary()`.
2. `native/qt6/src/importer.cpp` traverses selected files/folders, detects image sequences with `native/qt6/src/sequence_detector.{h,cpp}`, and writes rows through the `IAssetDatabase` contract from `native/qt6/src/i_asset_database.h`.
3. `native/qt6/src/db.{h,cpp}` emits change signals, and `native/qt6/src/assets_model.cpp` debounces reloads so Asset Manager views refresh after bulk work.

**File Manager Flow:**

1. `MainWindow::setupFileManagerUi()` in `native/qt6/src/mainwindow.cpp` builds the legacy File Manager shell around `QFileSystemModel` or `native/qt6/src/everything_folder_model.{h,cpp}` for the tree.
2. The newer pane abstraction in `native/qt6/src/file_manager_pane.{h,cpp}` packages the same concepts—toolbar, path bar, grid/list views, preview panel, navigation history—into a reusable unit for dual-pane workflows.
3. File selections feed preview widgets through `FileManagerPane::updatePreviewForIndex()` or the equivalent `MainWindow` handlers, using `native/qt6/src/media/tlrender_player.{h,cpp}` for video and `native/qt6/src/oiio_image_loader.{h,cpp}` for stills.

**Project Manager Flow:**

1. `MainWindow::setupProjectManagerUi()` in `native/qt6/src/mainwindow.cpp` initializes `ProjectDB::instance()` with a separate `projects.db` file under `QStandardPaths::AppDataLocation`.
2. `native/qt6/src/projects_model.{h,cpp}`, `native/qt6/src/project_folders_model.{h,cpp}`, and `native/qt6/src/project_assets_model.{h,cpp}` feed the project list, project tree, and asset views.
3. `native/qt6/src/project_manager_watcher.{h,cpp}` watches each project’s root folder, writes notifications into `native/qt6/src/project_db.{h,cpp}`, and `native/qt6/src/mainwindow.cpp` refreshes badges and current views when `newFilesDetected` or `filesRemoved` fires.

**State Management:**
- Persist UI/session state with `QSettings` in `native/qt6/src/mainwindow.cpp`, `native/qt6/src/file_manager_pane.cpp`, and `native/qt6/src/context_preserver.{h,cpp}`.
- Persist application content in SQLite via `native/qt6/src/db.{h,cpp}` and `native/qt6/src/project_db.{h,cpp}`.
- Keep transient cross-view services in singletons such as `LivePreviewManager::instance()` from `native/qt6/src/live_preview_manager.{h,cpp}`, `FileOpsQueue::instance()` from `native/qt6/src/file_ops.{h,cpp}`, and `ProgressManager::instance()` from `native/qt6/src/progress_manager.{h,cpp}`.

## Key Abstractions

**MainWindow as Composition Root:**
- Purpose: Centralize top-level wiring for all tabs and shared managers.
- Examples: `native/qt6/src/mainwindow.h`, `native/qt6/src/mainwindow.cpp`
- Pattern: One large coordinator owns widgets, models, service connections, and command handlers.

**IAssetDatabase:**
- Purpose: Share import logic between the Asset Manager and Project Manager stores.
- Examples: `native/qt6/src/i_asset_database.h`, `native/qt6/src/db.h`, `native/qt6/src/project_db.h`, `native/qt6/src/importer.cpp`
- Pattern: Interface-based persistence contract with two concrete singleton implementations.

**Qt Models + Proxy Models:**
- Purpose: Keep views bound to data roles instead of direct widget mutation.
- Examples: `native/qt6/src/assets_model.{h,cpp}`, `native/qt6/src/project_assets_model.{h,cpp}`, `native/qt6/src/sequence_grouping_proxy_model.{h,cpp}`, `native/qt6/src/project_sequence_grouping_proxy_model.{h,cpp}`
- Pattern: `QAbstractItemModel`/`QAbstractListModel` for source data, proxies for grouping and transformation.

**Manager Singletons:**
- Purpose: Share process-wide services.
- Examples: `native/qt6/src/live_preview_manager.{h,cpp}`, `native/qt6/src/file_ops.{h,cpp}`, `native/qt6/src/log_manager.{h,cpp}`, `native/qt6/src/progress_manager.{h,cpp}`, `native/qt6/src/context_preserver.{h,cpp}`
- Pattern: Singleton service with signals/slots and internal synchronization.

**Filesystem Watchers:**
- Purpose: React to external file changes.
- Examples: `native/qt6/src/project_folder_watcher.{h,cpp}`, `native/qt6/src/project_manager_watcher.{h,cpp}`, `QFileSystemWatcher` usage in `native/qt6/src/mainwindow.cpp`
- Pattern: Watcher objects debounce change bursts and notify models/UI through signals.

**Media Adapter Layer:**
- Purpose: Separate playback/rendering details from UI widgets.
- Examples: `native/qt6/src/media/tlrender_player.{h,cpp}`, `native/qt6/src/media/tlrender_viewport.{h,cpp}`, `native/qt6/src/oiio_image_loader.{h,cpp}`
- Pattern: Wrapper classes around external media libraries exposed as Qt-friendly objects.

## Entry Points

**Desktop Application:**
- Location: `native/qt6/src/main.cpp`
- Triggers: Launching the `kassetmanagerqt` executable from the target in `native/qt6/CMakeLists.txt`
- Responsibilities: Configure runtime, initialize logging/theme/db, migrate legacy storage, show `MainWindow`.

**Primary Window Shell:**
- Location: `native/qt6/src/mainwindow.cpp`
- Triggers: Construction from `native/qt6/src/main.cpp`
- Responsibilities: Build Asset Manager, File Manager, Project Manager, bind commands to models/services, persist and restore UI state.

**Test Executables:**
- Location: `native/qt6/tests/CMakeLists.txt`
- Triggers: `ctest` against targets such as `test_db`, `test_models`, `test_importer`, `test_live_preview_manager`, `test_platform_session`
- Responsibilities: Exercise individual layers without launching the full desktop app.

**Standalone Wayland Probe:**
- Location: `wayland_probe.cpp` and `wayland-probe/CMakeLists.txt`
- Triggers: Building/running the probe utility in `wayland-probe/`
- Responsibilities: Platform-specific graphics/session probing outside the main app.

## Error Handling

**Strategy:** Fail fast during bootstrap, log operational errors centrally, and surface user-facing issues through dialogs or status messages.

**Patterns:**
- Use `qCritical()` and early `return -1` in `native/qt6/src/main.cpp` for startup failures such as database initialization.
- Use `qWarning()` plus `LogManager::instance()` in service and UI code such as `native/qt6/src/importer.cpp`, `native/qt6/src/project_db.cpp`, and `native/qt6/src/mainwindow.cpp`.
- Use `QMessageBox` and `statusBar()->showMessage()` in `native/qt6/src/mainwindow.cpp` for recoverable user workflow errors.

## Cross-Cutting Concerns

**Logging:** Centralize Qt message output through `qInstallMessageHandler(customMessageHandler)` in `native/qt6/src/main.cpp` and `native/qt6/src/log_manager.{h,cpp}`.
**Validation:** Guard invalid indexes, missing files, invalid paths, and missing DB rows inline in handlers such as `MainWindow::onFolderSelected()` and `FileManagerPane::navigateToPath()`.
**Authentication:** Not applicable; no application authentication layer is present in `native/qt6/src/`.

---

*Architecture analysis: 2026-04-11*
