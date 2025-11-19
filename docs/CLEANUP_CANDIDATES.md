# Cleanup candidates (manual review required)

This file lists files that appear unused or legacy based on the current CMake/Qt 6 application and build pipeline.
They are **not deleted automatically**; please review each item before removing it from the repository or your working copy.

- The list intentionally excludes:
  - Everything under `third_party/` (required runtimes).
  - The `dist/` directory and packaging outputs (explicitly kept for local testing).
  - Build trees such as `native/qt6/build/` and ignored folders like `node_modules/` (these are already outside Git tracking or treated as build artifacts).

## 1. Very likely legacy / unused

### 1.1 `scripts/prepare-dependencies.ps1`
- Path: `scripts/prepare-dependencies.ps1`.
- Purpose in file: downloads portable PostgreSQL, Redis, and FFmpeg into a `resources` folder and ends with instructions to run `npm run build:all` and `npm run dist`.
- Evidence of being unused:
  - Not referenced from `docs/INSTALL.md`, `docs/TECH.md`, CI workflows, or `native/qt6/CMakeLists.txt`.
  - The current build pipeline uses `scripts/build-windows.ps1` and `third_party/*` paths instead of a `resources` directory or npm-based build commands.
- Impact if removed: should not affect the Qt application build or packaging; only affects a legacy Node/Electron-style dependency setup that is no longer wired into the repo.

### 1.2 `native/qt6/src/live_preview_manager_slots.cpp`
- Path: `native/qt6/src/live_preview_manager_slots.cpp`.
- Contents: defines `LivePreviewManager::onFFmpegFrameReady` and `LivePreviewManager::onFFmpegError` methods that use a `FFmpegPlayer::VideoFrame` type.
- Evidence of being unused:
  - The file is **not** listed in `native/qt6/CMakeLists.txt` inside the `qt_add_executable(kassetmanagerqt ...)` source list, so it is not compiled into the application.
  - No other file in the repo references `live_preview_manager_slots.cpp` or `FFmpegPlayer`; only `live_preview_manager.cpp` is included in the main app and tests.
  - The current preview pipeline is implemented with `GStreamerPlayer` and `OIIOImageLoader` (see `live_preview_manager.cpp` and `media/gstreamer_player.{h,cpp}`).
  - Also detected as an unreferenced source when comparing `git ls-files native/qt6/src` with sources listed in `native/qt6/CMakeLists.txt` and `native/qt6/tests/CMakeLists.txt`.
- Impact if removed: safe to delete as old FFmpeg-based LivePreviewManager slot code; removal will not change the current GStreamer-based preview behavior or builds.

### 1.3 `native/qt6/src/app.rc`
- Path: `native/qt6/src/app.rc`.
- Contents: Windows resource script (icon/version metadata) originally intended for the `kassetmanagerqt` executable.
- Evidence of being unused:
  - Not referenced as a source file in `native/qt6/CMakeLists.txt` or `native/qt6/tests/CMakeLists.txt` (the same unreferenced-source check flags `src/app.rc`).
  - The CMake build config wires Windows packaging and NSIS installers without mentioning `app.rc`, and the executable currently builds fine without it.
- Impact if removed: removes an unused resource script; it should not affect the current build pipeline or runtime behavior, but you can keep it if you plan to reintroduce custom Windows resources.

### 1.4 Test environment helper scripts (`scripts/test-setup.ps1`, `scripts/test-cleanup.ps1`)
- Paths:
  - `scripts/test-setup.ps1`
  - `scripts/test-cleanup.ps1`
- Purpose in files:
  - `test-setup.ps1`: starts portable PostgreSQL and Redis from a local `resources` folder and generates a `.env` file under a `backend` directory.
  - `test-cleanup.ps1`: stops the PostgreSQL and Redis processes started by `test-setup.ps1`.
- Evidence of being unused:
  - Both scripts assume the presence of a top-level `resources/` folder and `backend`/`frontend` directories, none of which exist in the current repository.
  - They instruct you to run `npm run prepare:deps`, `cd backend && npm run ...`, and `cd frontend && npm run ...`, but there is no `package.json`, `backend/`, or `frontend/` directory in the repo.
  - Not referenced in `docs/INSTALL.md`, `docs/TECH.md`, CI workflows, or the Qt/CMake build scripts.
- Impact if removed: safe to delete as legacy test-environment helpers for a removed Node/Redis/PostgreSQL backend; they are not used by the current Qt 6 desktop application or its CI.

## 2. Notes on untracked / generated content

The following items are **not tracked by Git** in this repository but can be safely deleted from a working copy when cleaning up disk usage:
- `native/qt6/build/` – CMake build tree (recreated by running the CMake/configure steps).
- `dist/` contents – build/test artifacts; keep if you rely on existing portable builds, otherwise they can be regenerated via `scripts/build-windows.ps1 -Package`.
- `node_modules/` – local Node/Electron dependencies; not used by the Qt 6 application or current CI/build flow and are ignored via `.gitignore`.

These paths are mentioned here for convenience only; Git will not remove them because they are not part of the tracked history.

## 3. Configuration entries to review (not files to delete)

### 3.1 `.github/dependabot.yml` npm entry for `/web`
- Path: `.github/dependabot.yml`.
- Details:
  - Contains an `npm` update configuration with `directory: "/web"`.
  - The repository does not contain a `web/` directory or any tracked `package.json`.
- Suggested action:
  - Consider removing or updating the `npm` `/web` entry while keeping the `github-actions` and `pip` entries, since those are part of the active workflow.

