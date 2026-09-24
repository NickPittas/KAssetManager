# Codebase Structure

**Analysis Date:** 2026-04-11

## Directory Layout

```text
KAssetManager/
├── .github/                 # CI workflows and Copilot guidance
├── .planning/codebase/      # Generated mapping documents for GSD workflows
├── docs/                    # Developer and user documentation
├── installer/               # NSIS installer definition
├── native/qt6/              # Main Qt 6 desktop application
│   ├── cmake/               # CMake helper modules
│   ├── src/                 # Production C++ source and headers
│   │   └── media/           # tlRender-specific media adapters/widgets
│   └── tests/               # QtTest executables
├── scripts/                 # Build and dependency bootstrap scripts
├── third_party/             # Bundled external runtimes and SDKs
├── wayland-probe/           # Separate platform probe utility build tree
└── wayland_probe.cpp        # Standalone Wayland probe source
```

## Directory Purposes

**`native/qt6/src/`:**
- Purpose: Main application code.
- Contains: Bootstrap, window shell, models, dialogs, delegates, watchers, services, database code, preview logic.
- Key files: `native/qt6/src/main.cpp`, `native/qt6/src/mainwindow.cpp`, `native/qt6/src/db.cpp`, `native/qt6/src/project_db.cpp`, `native/qt6/src/importer.cpp`, `native/qt6/src/live_preview_manager.cpp`.

**`native/qt6/src/media/`:**
- Purpose: Media backend wrappers and rendering widgets.
- Contains: tlRender player wrappers and viewport widgets.
- Key files: `native/qt6/src/media/tlrender_player.h`, `native/qt6/src/media/tlrender_viewport.h`, `native/qt6/src/media/tlrender_widget.h`.

**`native/qt6/tests/`:**
- Purpose: Isolated QtTest executables for core subsystems.
- Contains: `test_*.cpp` sources plus the test build manifest.
- Key files: `native/qt6/tests/CMakeLists.txt`, `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_models.cpp`, `native/qt6/tests/test_importer.cpp`, `native/qt6/tests/test_live_preview_manager.cpp`.

**`native/qt6/cmake/`:**
- Purpose: Packaging/build helpers.
- Contains: Deployment support modules.
- Key files: `native/qt6/cmake/DeployQt.cmake`.

**`scripts/`:**
- Purpose: Developer build, packaging, and dependency fetch entry points.
- Contains: PowerShell scripts for Windows builds and dependency downloads.
- Key files: `scripts/build-windows.ps1`, `scripts/build-installer.ps1`, `scripts/download-everything-sdk.ps1`, `scripts/fetch-ffmpeg.ps1`.

**`third_party/`:**
- Purpose: Checked-in third-party runtimes and source trees used by builds.
- Contains: `tlRender`, Everything SDK, FFmpeg, ImageMagick assets.
- Key files: `third_party/tlRender-install-Release/`, `third_party/tlRender-src/`, `third_party/everything/`, `third_party/ffmpeg/`.

**`docs/`:**
- Purpose: Human-facing reference material.
- Contains: Architecture, dependencies, install docs, user guide, codemap.
- Key files: `docs/ARCHITECTURE.md`, `docs/DEVELOPER_GUIDE.md`, `docs/DEPENDENCIES.md`, `docs/USER_GUIDE.md`.

**`.github/`:**
- Purpose: Automation and repo policy.
- Contains: CI workflows and machine guidance.
- Key files: `.github/workflows/ci.yml`, `.github/copilot-instructions.md`.

**`.planning/codebase/`:**
- Purpose: Generated planning artifacts used by GSD orchestration.
- Contains: Focused mapping documents such as `ARCHITECTURE.md` and `STRUCTURE.md`.
- Key files: `.planning/codebase/ARCHITECTURE.md`, `.planning/codebase/STRUCTURE.md`.

**`installer/`:**
- Purpose: Native installer packaging metadata.
- Contains: NSIS script.
- Key files: `installer/kassetmanager.nsi`.

**`wayland-probe/`:**
- Purpose: Platform-specific probe utility build directory.
- Contains: Generated CMake/Ninja build output for the probe.
- Key files: `wayland-probe/build.ninja`, `wayland-probe/CMakeCache.txt`.

## Key File Locations

**Entry Points:**
- `native/qt6/src/main.cpp`: Desktop application bootstrap.
- `native/qt6/src/mainwindow.cpp`: Main application shell and command wiring.
- `native/qt6/tests/CMakeLists.txt`: Test executable entrypoint definitions.
- `wayland_probe.cpp`: Standalone Wayland probe source.

**Configuration:**
- `native/qt6/CMakeLists.txt`: Main build graph, dependency detection, install rules, compile definitions.
- `native/qt6/CMakePresets.json`: Preset-based CMake invocation setup.
- `.github/workflows/ci.yml`: CI matrix for Windows, Ubuntu, and coverage.
- `.clang-tidy`: Static analysis configuration.
- `.github/copilot-instructions.md`: Repository-specific implementation constraints.

**Core Logic:**
- `native/qt6/src/db.cpp`: Asset Manager SQLite schema and CRUD.
- `native/qt6/src/project_db.cpp`: Project Manager SQLite schema and CRUD.
- `native/qt6/src/importer.cpp`: Shared import pipeline through `IAssetDatabase`.
- `native/qt6/src/live_preview_manager.cpp`: Cached async decode service.
- `native/qt6/src/file_ops.cpp`: Background file operation queue.
- `native/qt6/src/sequence_detector.cpp`: Sequence detection and pattern utilities.

**Testing:**
- `native/qt6/tests/test_db.cpp`: Database behavior.
- `native/qt6/tests/test_models.cpp`: Model behavior.
- `native/qt6/tests/test_importer.cpp`: Import pipeline.
- `native/qt6/tests/test_live_preview_manager.cpp`: Preview manager behavior.
- `native/qt6/tests/test_platform_session.cpp`: Linux session detection helper coverage.

## Naming Conventions

**Files:**
- Use lowercase snake_case for production files: `native/qt6/src/live_preview_manager.cpp`, `native/qt6/src/project_manager_watcher.h`.
- Keep `.h`/`.cpp` pairs together with the same basename: `native/qt6/src/db.h` + `native/qt6/src/db.cpp`.
- Name tests with the `test_*.cpp` prefix: `native/qt6/tests/test_importer.cpp`.
- Keep media adapter files under the `media/` subdirectory with a shared `tlrender_` prefix: `native/qt6/src/media/tlrender_player.cpp`.

**Directories:**
- Keep platform/build directories noun-based and lowercase: `native/qt6/`, `scripts/`, `docs/`, `third_party/`.
- Use a focused nested directory only when a subsystem has a distinct external integration boundary, as in `native/qt6/src/media/`.

## Where to Add New Code

**New Feature:**
- Primary code: `native/qt6/src/`.
- Tests: `native/qt6/tests/` with a new `test_*.cpp` target added in `native/qt6/tests/CMakeLists.txt`.

**New Component/Module:**
- Implementation: Add a paired `native/qt6/src/<feature>.h` and `native/qt6/src/<feature>.cpp`, then register both in `native/qt6/CMakeLists.txt`.

**Utilities:**
- Shared helpers: `native/qt6/src/utils.{h,cpp}`, `native/qt6/src/file_utils.{h,cpp}`, or a new focused helper pair in `native/qt6/src/`.

**New UI Surface:**
- Main-window-integrated feature: wire it through `native/qt6/src/mainwindow.{h,cpp}` and existing models/services.
- Reusable file-browser subview: extend `native/qt6/src/file_manager_pane.{h,cpp}` if the behavior belongs to a pane rather than the whole shell.

**New Data Model:**
- Asset-library data: place model code beside `native/qt6/src/assets_model.{h,cpp}` and `native/qt6/src/virtual_folders.{h,cpp}`.
- Project-manager data: place model code beside `native/qt6/src/projects_model.{h,cpp}`, `native/qt6/src/project_assets_model.{h,cpp}`, and `native/qt6/src/project_folders_model.{h,cpp}`.

**New Media Integration:**
- Backend wrappers and rendering widgets: `native/qt6/src/media/`.
- Thumbnail/preview helpers: `native/qt6/src/live_preview_manager.{h,cpp}`, `native/qt6/src/oiio_image_loader.{h,cpp}`, or a sibling helper in `native/qt6/src/`.

## Special Directories

**`native/qt6/src/media/`:**
- Purpose: Isolate tlRender-facing code from general UI and model code.
- Generated: No.
- Committed: Yes.

**`native/qt6/tests/`:**
- Purpose: Keep test executables separate from production targets.
- Generated: No.
- Committed: Yes.

**`native/qt6/cmake/`:**
- Purpose: Hold reusable CMake modules instead of inlining deployment logic into `native/qt6/CMakeLists.txt`.
- Generated: No.
- Committed: Yes.

**`third_party/`:**
- Purpose: Vendor external dependencies and runtime payloads used by local builds and packaging.
- Generated: No.
- Committed: Yes.

**`build-linux/`, `build-linux-make/`, `wayland-probe/`:**
- Purpose: Store generated local build outputs and probe artifacts.
- Generated: Yes.
- Committed: Mixed; treat them as build artifacts rather than source locations.

**`.planning/codebase/`:**
- Purpose: Store generated repository maps for planning/execution workflows.
- Generated: Yes.
- Committed: Yes.

---

*Structure analysis: 2026-04-11*
