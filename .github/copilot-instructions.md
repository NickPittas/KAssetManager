## KAsset Manager — AI agent guide

This repo is a native Windows app built with Qt 6 (C++). Focus on productivity by following the project’s architecture, threading, build, and testing patterns below. Keep changes minimal, safe, and aligned with existing conventions.

### Architecture at a glance
- UI: Qt Widgets app (`native/qt6/src/mainwindow.*`, models under `src/*_model*.{h,cpp}`) runs on the UI thread only.
- Background work: Use QtConcurrent/QThreadPool. Examples:
  - Live previews: `live_preview_manager.{h,cpp}` decodes off-UI, caches pixmaps via `QCache` (LRU), posts results via queued signals.
  - Import: `importer.{h,cpp}` batches DB inserts inside transactions and emits progress; currently uses main thread DB connection.
- Logging: `qInstallMessageHandler(customMessageHandler)` in `main.cpp` forwards all Qt logs to `LogManager` (`log_manager.{h,cpp}`), which keeps a thread-safe ring buffer and writes `app.log` next to the exe. Don’t add new message handlers.
- DB: SQLite via QtSql (`db.{h,cpp}`)
  - Connection is single-threaded. Do not share QSqlDatabase/QSqlQuery across threads; open/use per-thread if you add background DB work.
  - Batch heavy ops inside transactions; prefer prepared statements and safe IN-clause construction (see `assets_model.cpp` and `DB::getAssetIdsInFolder`).
- Data location and migration: Use `QStandardPaths::AppDataLocation`. `main.cpp` migrates legacy `./data/kasset.db` to `%AppData%/KAsset/.../kasset.db` and writes minidumps on crash (Windows-only SEH filter).
- External integrations:
  - FFmpeg: optional (headers/libs) for decode/log suppression; linked when found. DLLs copied into `dist/portable/bin` during packaging.
  - OpenImageIO: optional; guards via `HAVE_OPENIMAGEIO` for advanced image formats.
  - Everything SDK: `everything_search*.{h,cpp}`, DLL packaged if present in `third_party/everything/Everything64.dll`.

### Models, filtering, and DnD patterns
- Examples: `assets_model.{h,cpp}` exposes roles (`IdRole`, `FilePathRole`, `PreviewStateRole`, etc.), coalesces resets, and debounces reloads with a single `QTimer`.
- Filtering: keep UI responsive by rebuilding filters in-memory and querying DB once per reload (`AssetsModel::query` then `rebuildFilter`).
- Drag-and-drop:
  - Internal: `application/x-kasset-asset-ids` (QDataStream of IDs), `application/x-kasset-sequence-urls` (expanded paths).
  - External: provide folder path for sequences; file URL for single files; also provide text/uri-list for DCCs.

### Build, run, and package (Windows)
- Preferred path is the helper script. It configures, builds, installs to a staging dir, verifies the exe, and produces a portable package:
  - `scripts/build-windows.ps1 -Generator Ninja -Package`
  - Output portable app: `dist/portable/bin/kassetmanagerqt.exe`
- Environment variables auto-detected when possible:
  - `VCPKG_ROOT` (e.g., `C:\vcpkg`), `FFMPEG_ROOT` (with include/, lib/, bin/), `IMAGEMAGICK_ROOT`.
- CMake toggles (see `native/qt6/CMakeLists.txt`): `BUILD_APP`, `BUILD_TESTS`, `ENABLE_CLANG_TIDY`, `ENABLE_COVERAGE`, plus sanitizer flags for GCC/Clang.

### Tests and how to run them
- Framework: QtTest. Sources in `native/qt6/tests/` with per-test executables defined in `tests/CMakeLists.txt`.
- Typical local flow (Windows, via the build script): tests are installed in `native/qt6/build/<gen>/install_run/bin`; `ctest` is configured by CMake. Some tests set `HAVE_OPENIMAGEIO=0`/`HAVE_FFMPEG=0` to avoid heavy deps.

### Conventions and safety
- Threading: Never block the UI thread. Use signals/slots (queued connections) to return results. If you must add DB work off-UI, first introduce a per-thread QSqlDatabase and keep it private to that thread.
- Logging: Use `qDebug/qWarning/qCritical`; `LogManager` captures and persists. Avoid sensitive data in logs.
- Filesystem ops: Use existing helpers (e.g., `file_ops*.{h,cpp}`, `drag_utils.{h,cpp}`) and OS-native behavior for Explorer interoperability.
- Feature flags: Gate optional deps with compile definitions (`HAVE_OPENIMAGEIO`, `HAVE_FFMPEG`, `HAVE_QT_PDF[_WIDGETS]`). Follow the existing preprocessor guard style (defined(...) && ...).

### When adding features
- Wire UI via `MainWindow` and existing models. Keep long work off-UI; prefer `QtConcurrent::run` and post results back.
- Reuse the packaging hooks in `CMakeLists.txt` to deploy Qt plugins, vcpkg DLLs, FFmpeg, Everything DLL, and icons.
- Follow the existing drag-and-drop and sequence handling conventions when integrating new viewers/converters.

References to study first: `docs/ARCHITECTURE.md`, `docs/DEVELOPER_GUIDE.md`, `native/qt6/CMakeLists.txt`, `src/main.cpp`, `src/assets_model.cpp`, `src/live_preview_manager.cpp`, `scripts/build-windows.ps1`.
