# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Commands you’ll use most

Windows-first workflow (Qt 6, MSVC). The helper scripts configure CMake, build, stage runtime DLLs, and optionally package a portable build.

### Build + package (Windows, preferred)
```powershell
# From repo root
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package

# Launch packaged app after success
.\dist\portable\bin\kassetmanagerqt.exe
```

Notes
- vcpkg is expected at `C:\vcpkg` (script auto-wires toolchain and DLL copy).
- Optional env vars auto-detected when present: `FFMPEG_ROOT`, `IMAGEMAGICK_ROOT`.

### Build without packaging (Windows)
```powershell
# Configure + build (Release), no packaging
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja

# Install to staging (where tests and Qt plugins expect to run)
cmake --install native/qt6/build/ninja --prefix native/qt6/build/ninja/install_run --config Release

# Run the built app from staging (if BUILD_APP=ON)
.\native\qt6\build\ninja\install_run\bin\kassetmanagerqt.exe
```

### Tests (QtTest via CTest)
```powershell
# Configure tests (Windows)
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 2
cmake --install build --prefix build\install_run

# Run all tests
ctest --test-dir build --output-on-failure

# Run a single test by name (regex)
ctest --test-dir build -R test_models --output-on-failure

# Or run a single test executable directly (after install step)
.\build\install_run\bin\test_models.exe
```

Linux (tests-only) quick reference
```bash
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

### Static analysis and coverage
```powershell
# Enable clang-tidy at configure time (runs during the build)
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DENABLE_CLANG_TIDY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
```
```bash
# (Linux) Configure with sanitizers or coverage
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure

# Coverage job uses -DENABLE_COVERAGE=ON then gcovr (see .github/workflows/ci.yml)
```

### Third‑party helper scripts
```powershell
# Fetch an FFmpeg build into third_party/ffmpeg (used by Convert tools only)
.\scripts\fetch-ffmpeg.ps1

# Download Everything SDK DLL into third_party/everything
.\scripts\download-everything-sdk.ps1
```

### CMake options you may toggle
- `BUILD_APP` (ON/OFF) — build the GUI app (CI uses OFF)
- `BUILD_TESTS` (ON/OFF)
- `ENABLE_CLANG_TIDY` (ON)
- `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_COVERAGE` (Clang/GCC)

## High‑level architecture (what matters when editing code)

Desktop app built with Qt 6 Widgets (C++20). Core subsystems and constraints below are enforced across the codebase.

- UI thread only for widgets/models
  - `MainWindow` owns views and wiring; do not block the UI thread.
  - Long operations must run off‑UI; communicate back via queued signals/slots.

- Background work and preview pipeline
  - `LivePreviewManager` schedules decode work via `QtConcurrent`/`QThreadPool` and maintains an LRU pixmap cache.
- Playback for video and image sequences is tlRender-based (`media/tlrender_player.*`, `media/tlrender_viewport.*`).
  - Still images use OpenImageIO when available for consistent decoding; Qt readers are fallback.
- Optional features are guarded with compile definitions: `HAVE_TLRENDER`, `HAVE_OPENIMAGEIO`, `HAVE_FFMPEG`, `HAVE_QT_PDF[_WIDGETS]`.

- Database layer (SQLite via QtSql)
  - `DB` is a singleton managing schema, migrations, and CRUD.
  - Connections are not thread‑safe across threads; if you add off‑UI DB work, open a per‑thread `QSqlDatabase` and keep it private to that thread.
  - Batch heavy operations in transactions; use prepared statements.

- Logging (centralized)
  - `main.cpp` installs a single Qt message handler captured by `LogManager`.
  - Do not install additional message handlers; use `qDebug/qWarning/qCritical` normally.

- File Manager and operations
  - Explorer‑style views use custom delegates/views; heavy work (icons, metadata, previews) is demand‑driven to keep scrolling fast.
  - Shell file ops are wrapped by `FileOps` with a background queue and UI progress dialog.

- Packaging/runtime layout (Windows)
  - `cmake --install` stages into `native/qt6/build/<gen>/install_run/`.
- Packaging copies Qt plugins, vcpkg DLLs, optional FFmpeg/ImageMagick, tlRender runtime, and Everything DLL if present.
  - Portable build appears under `dist/portable/`.

## Agent‑facing conventions (summarized from .github/copilot‑instructions.md)

- Threading
  - Never block the UI thread. Use `QtConcurrent::run` or worker objects and queued signals.
  - If adding background DB work, create a per‑thread `QSqlDatabase` and do not share queries across threads.

- Logging
  - Keep the single global message handler; don’t add new ones.

- Feature gating and deps
  - Use existing compile definitions (`HAVE_*`) consistently; don’t introduce ad‑hoc flags.
  - Everything SDK DLL is optional; if present at `third_party/everything/Everything64.dll` it’s bundled by packaging.

- Build hooks
  - Reuse the installer/deploy logic in `native/qt6/CMakeLists.txt` and `scripts/build-windows.ps1` rather than duplicating deploy steps.

## Pointers to deeper docs
- README: quick start and feature overview (`README.md`)
- Architecture: threading/I‑O, logging, persistence (`docs/ARCHITECTURE.md`)
- Developer guide: build/test options, CI jobs, security highlights (`docs/DEVELOPER_GUIDE.md`)
- Dependencies and runtimes (`docs/DEPENDENCIES.md`)
- Code map by feature (`docs/CODEMAP.md`)
