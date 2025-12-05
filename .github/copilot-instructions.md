## KAsset Manager - AI agent guide

Native Windows asset manager built with Qt 6 (C++20). Keep changes minimal and aligned with existing patterns.

### Architecture
- **UI thread only**: Qt Widgets (`mainwindow.*`, `*_model*.{h,cpp}`). Never block with I/O or heavy CPU.
- **Background work**: `QtConcurrent::run` / QThreadPool. Return results via queued signals.
  - `live_preview_manager.*` - decodes off-UI, uses `QCache` (LRU ~512MB), delivers via signals
  - `importer.*` - batches DB inserts in transactions, emits progress (currently main-thread DB)
- **DB**: SQLite via QtSql (`db.{h,cpp}`). Single-threaded connection - never share QSqlDatabase/QSqlQuery across threads. Use transactions for bulk ops; prefer prepared statements (see `assets_model.cpp`).
- **Logging**: Single handler in `main.cpp` -> `LogManager`. Use `qDebug/qWarning/qCritical`; don't add handlers.
- **Data path**: `QStandardPaths::AppDataLocation` (`%AppData%/KAsset/...`). Legacy migration in `main.cpp`.

### Media backends
| Purpose | Backend | Guard macro |
|---------|---------|-------------|
| Video/sequence playback | GStreamer | `HAVE_GSTREAMER` |
| Still images | OpenImageIO | `HAVE_OPENIMAGEIO` |
| Conversion only | FFmpeg | `HAVE_FFMPEG` |
| PDF viewing | Qt PDF | `HAVE_QT_PDF[_WIDGETS]` |

Guard optional features: `#if defined(HAVE_...) && HAVE_...`

### Build (Windows)
```powershell
scripts/build-windows.ps1 -Generator Ninja -Package
# Output: dist/portable/bin/kassetmanagerqt.exe
```
- Auto-detects `VCPKG_ROOT`, `FFMPEG_ROOT`, `IMAGEMAGICK_ROOT`, bundled GStreamer in `third_party/gstreamer`
- CMake options: `BUILD_APP`, `BUILD_TESTS`, `ENABLE_CLANG_TIDY`, `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_COVERAGE`

### Tests
- Framework: QtTest (`native/qt6/tests/`)
- Run: after build, executables in `native/qt6/build/<gen>/install_run/bin`; use `ctest`
- Some tests compile with `HAVE_OPENIMAGEIO=0`/`HAVE_FFMPEG=0` to skip heavy deps

### Key patterns
- **Models**: `AssetsModel` exposes roles (`IdRole`, `FilePathRole`, etc.), debounces reloads via `QTimer` (100ms)
- **Sequences**: `sequence_detector.*` handles image sequence detection, gap analysis, and version tracking. Sequences stored as single DB rows with frame range metadata.
- **Annotations**: `annotation_layer.*` and `annotation_items.*` provide frame-accurate drawing tools (pen, text, shapes, arrows) with per-frame storage and export.
- **Drag-and-drop**: Internal MIME `application/x-kasset-asset-ids`; external uses `text/uri-list` + folder paths for sequences
- **File ops**: Use `file_ops.*`, `drag_utils.*`; shell integration for Explorer interop
- **Everything SDK**: Fast disk search via `everything_search*.{h,cpp}`, DLL in `third_party/everything/`

### File Manager performance
- **Tree expansion**: Folder-only - never trigger file enumeration, shell icon lookups, or metadata/preview work when expanding nodes
- **Preview/Info panes**: Only invoke `LivePreviewManager` and metadata readers on explicit selection AND when panes are visible
- **Lazy loading**: Decode only when items enter the viewport; use `FileOpsQueue` for shell operations

### Adding features
1. Wire UI via `MainWindow` and existing models
2. Long work -> `QtConcurrent::run`, post back via signals
3. New optional deps -> add CMake detection + `HAVE_*` guard
4. Reuse packaging hooks in `CMakeLists.txt` for DLL deployment

### Key files
`docs/ARCHITECTURE.md`, `docs/DEVELOPER_GUIDE.md`, `native/qt6/CMakeLists.txt`, `src/main.cpp`, `src/assets_model.cpp`, `src/live_preview_manager.cpp`, `scripts/build-windows.ps1`
