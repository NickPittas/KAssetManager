# External Integrations

**Analysis Date:** 2026-04-11

## APIs & External Services

**Remote HTTP APIs:**
- Not detected. Searches across `native/qt6/src/*.cpp` and `native/qt6/src/*.h` did not show `QNetworkAccessManager`, OAuth clients, webhook handlers, or application HTTP API calls.

**Desktop/Search Services:**
- Everything Search - Windows desktop search service used for disk-wide filename search in `native/qt6/src/everything_search.cpp` and `native/qt6/src/everything_search.h`.
  - SDK/Client: `Everything64.dll`, dynamically loaded with `QLibrary` in `native/qt6/src/everything_search.cpp`.
  - Auth: Not applicable; availability depends on local DLL presence and a running Everything service.

**Media Toolchain:**
- tlRender - playback/render stack integrated at build time in `native/qt6/CMakeLists.txt`, with related wrappers in `native/qt6/src/media/tlrender_player.cpp`, `native/qt6/src/media/tlrender_widget.cpp`, and `native/qt6/src/media/tlrender_viewport.cpp`.
  - SDK/Client: tlRender CMake package targets from the local `third_party/tlRender-*` install referenced in `native/qt6/CMakeLists.txt`.
  - Auth: Not applicable.
- FFmpeg / ffprobe - external executables and libraries used for conversion, probing, and metadata extraction in `native/qt6/src/media_convert_dialog.cpp`, `native/qt6/src/media_converter_worker.cpp`, and `native/qt6/src/video_metadata.cpp`.
  - SDK/Client: FFmpeg CLI (`ffmpeg`, `ffprobe`) and libav* headers/libs behind `HAVE_FFMPEG` in `native/qt6/src/video_metadata.cpp` and `native/qt6/src/live_preview_manager.cpp`.
  - Auth: Not applicable.
- ImageMagick - external conversion tool for single-image format conversion in `native/qt6/src/media_convert_dialog.cpp` and `native/qt6/src/media_converter_worker.cpp`.
  - SDK/Client: `magick.exe` or `magick` located from app dir, `third_party`, env vars, or PATH in `native/qt6/src/media_convert_dialog.cpp`.
  - Auth: Not applicable.
- Office document readers - local file-format integrations for OOXML ZIP containers and legacy OLE compound documents in `native/qt6/src/office_preview.cpp`.
  - SDK/Client: `MINIZIP::minizip-ng` plus Windows COM storage APIs (`StgOpenStorageEx`) in `native/qt6/src/office_preview.cpp`.
  - Auth: Not applicable.

## Data Storage

**Databases:**
- SQLite database stored locally as `kasset.db` under `QStandardPaths::AppDataLocation`, initialized in `native/qt6/src/main.cpp` and opened with `QSqlDatabase::addDatabase("QSQLITE")` in `native/qt6/src/db.cpp`.
  - Connection: local filesystem path, not an environment variable.
  - Client: Qt SQL (`Qt6::Sql`) through `native/qt6/src/db.cpp` and `native/qt6/CMakeLists.txt`.

**File Storage:**
- Local filesystem only. Asset source files remain on disk, version copies are stored under `AppDataLocation/versions` in `native/qt6/src/db.cpp`, and thumbnail cache files are stored under `AppDataLocation/thumbnail_cache` in `native/qt6/src/thumbnail_cache_manager.cpp`.

**Caching:**
- In-memory thumbnail/frame cache in `native/qt6/src/live_preview_manager.cpp`.
- Persistent on-disk thumbnail cache in `native/qt6/src/thumbnail_cache_manager.cpp`.

## Authentication & Identity

**Auth Provider:**
- None. The application is a local desktop app and no user login, OAuth provider, or API token exchange is implemented in `native/qt6/src`.
  - Implementation: Not applicable.

## Monitoring & Observability

**Error Tracking:**
- Local crash dump and crash log generation on Windows in `native/qt6/src/main.cpp` using `MiniDumpWriteDump`, `crash.dmp`, and `crash.log` under `QStandardPaths::AppDataLocation`.

**Logs:**
- Centralized Qt message handling routed through `customMessageHandler` and `LogManager` in `native/qt6/src/main.cpp` and `native/qt6/src/log_manager.cpp`.
- Persistent app log written to `app.log` next to the executable in `native/qt6/src/log_manager.cpp`.

## CI/CD & Deployment

**Hosting:**
- No server hosting platform is detected. Distribution is a packaged desktop binary created by `scripts/build-windows.ps1` and `scripts/build-installer.ps1`.

**CI Pipeline:**
- GitHub Actions CI in `.github/workflows/ci.yml`.
- Windows and Ubuntu jobs configure/build/install QtTest targets, and a separate Ubuntu job generates coverage with `gcovr` in `.github/workflows/ci.yml`.

## Environment Configuration

**Required env vars:**
- `VCPKG_ROOT` - vcpkg toolchain root in `native/qt6/CMakeLists.txt` and `.github/workflows/ci.yml`.
- `Qt6`, `Qt6_DIR`, or `QT_DIR` - Qt discovery inputs in `native/qt6/CMakePresets.json` and `scripts/build-windows.ps1`.
- `FFMPEG_ROOT` - optional FFmpeg runtime/tool discovery in `scripts/build-windows.ps1` and `native/qt6/src/media_convert_dialog.cpp`.
- `IMAGEMAGICK_ROOT` or `MAGICK_ROOT` - optional ImageMagick discovery in `scripts/build-windows.ps1` and `native/qt6/src/media_convert_dialog.cpp`.
- `XDG_SESSION_TYPE` and `QT_QPA_PLATFORM` - Linux/Wayland behavior checks in `native/qt6/src/platform_session.h` and `native/qt6/src/main.cpp`.

**Secrets location:**
- Not applicable for application runtime because no remote-service credentials are used.
- Build inputs are expected from shell or CI environment variables, not from committed secret files; no `.env*` files were detected in this worktree.

## Webhooks & Callbacks

**Incoming:**
- None detected.

**Outgoing:**
- None detected.

---

*Integration audit: 2026-04-11*
