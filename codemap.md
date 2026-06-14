# KAssetManager repository codemap

This codemap is a current navigation snapshot for maintainers. It is not a substitute for reading the live source path before changing behavior.

## Root

- `README.md` — current product overview, build/package quickstart, cleanup policy.
- `AGENTS.md` — repository workflow rules for agent-assisted work.
- `CMakeLists.txt` / `native/qt6/CMakeLists.txt` — native Qt build entry points.
- `scripts/` — packaging/build automation.
- `docs/` — user, install, architecture, dependency, and AppImage documentation.
- `third_party/` — checked-in third-party payloads used by local builds. Some package config files are intentionally relocatable so this checkout does not depend on old absolute worktree paths.

## Application source: `native/qt6/src/`

### Main window and UI orchestration

- `mainwindow.h/.cpp` owns the primary application window and most cross-panel wiring.
- File Manager responsibilities inside `MainWindow` include tree/favorites/path-bar routing, active-pane state, dual-pane creation, splitter persistence, preview/player teardown, and toolbar synchronization.
- Current dual-pane behavior: tree selection, tree activation, favorites activation, path-bar entry, and folder double-click navigation call `navigateActiveFmPaneToPath(...)`, so navigation targets the active explorer pane.

### File Manager panes and scrubbing

- `file_manager_pane.h/.cpp` encapsulates each explorer pane: path, view mode, selection, preview sizing, embedded preview/player widgets, and event filters.
- `grid_scrub.h/.cpp` implements Ctrl-hover grid scrubbing. It ignores synthetic cursor-warp mouse moves, avoids duplicate requests per move, and exposes cleanup through `endScrub()`.
- `live_preview_manager.h/.cpp` provides frame requests and preview decode management. Scrub requests can bypass global preview suspension so hover scrubbing remains responsive.
- `icon_utils.h/.cpp` provides generated vector icons and `KFileIconProvider`. Provider icons are cached internally and do not depend on platform icon themes, which keeps folder/tree/list icons visible in AppImages.

### Preview/playback

- `preview_overlay.*`, `live_preview_manager.*`, and `thumbnail_generator_worker.*` handle image/video preview surfaces, thumbnails, and async decode requests.
- `player_lab/` contains the GPU/FFmpeg player implementation and validation harnesses used by the main app integration.
- Exit teardown in `MainWindow::~MainWindow()` disconnects/block-signals from player objects, detaches video widgets, unloads media while UI objects are still valid, and deletes the secondary pane deterministically.

### Data and models

- `db*`, `database_worker.*`, and `project_db.*` provide SQLite access and async worker paths.
- `assets_model.*`, `project_assets_model.*`, `virtual_folders.*`, `tags_model.*`, and project model files back the asset library and project views.
- `importer.*`, `import_controller.*`, `project_import_worker.*`, and watcher files handle import/project-folder workflows.

### Runtime helpers

- `runtime_paths.*` resolves app data and runtime paths.
- `theme_manager.*`, `settings_dialog.*`, `user_guide_dialog.*`, and UI helper files provide supporting dialogs and styling.

## Packaging and generated outputs

### Linux AppImage

- `scripts/build-linux-appimage.sh` configures/builds/installs the Qt app into `build-linux-appimage/AppDir/usr` with `CMAKE_INSTALL_LIBDIR=lib`.
- `scripts/package-appimage.sh` creates `AppRun`, copies desktop/icon metadata, harvests Qt plugins and Qt runtime libraries via `qtpaths` when linuxdeploy is not explicitly configured, and invokes `appimagetool`.
- The controlled `qtpaths` path is preferred on the current Fedora baseline. Broad linuxdeploy dependency sweeps can over-bundle low-level host libraries and produce an AppImage that crashes before `main`.
- Generated outputs: `build-linux-appimage/`, `KAssetManager-*.AppImage`, and local downloaded tools under `tools/appimage/`. These are ignored and reproducible.

### Local build trees

- `native/qt6/build/`, `native/qt6/build-integration/`, `build-test/`, and player-lab build folders are generated CMake build outputs and safe to remove when cleaning.

## Documentation map

- `docs/USER_GUIDE.md` — user-facing workflow guide.
- `docs/INSTALL.md` — install/setup details.
- `docs/APPIMAGE_CREATION.md` — Linux package creation and verification workflow.
- `docs/ARCHITECTURE.md` — architecture and threading/I/O notes.
- `docs/DEPENDENCIES.md` — dependency inventory and security notes.
- `docs/CODEMAP.md` and nested folder codemaps may exist as older snapshots; verify against live files before relying on them.

## Cleanup policy

Remove or leave untracked generated artifacts: AppImages, AppDir/build trees, `scratch/`, `graphify-out/`, recorder sessions/logs, validation screenshots, temporary thumbnails, and downloaded local tool binaries. Keep source, documentation, scripts, and intentional relocatable third-party package metadata.
