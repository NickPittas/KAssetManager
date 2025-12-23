## KAsset Manager - AI agent guide

Native Windows asset manager built with Qt 6 (C++20). Sources in `native/qt6/src/`. Keep changes minimal and aligned with existing patterns.

### Architecture
- **UI thread only**: Qt Widgets (`mainwindow.*`, `*_model*.{h,cpp}`). Never block with I/O or heavy CPU work.
- **Background work**: `QtConcurrent::run` / QThreadPool. Return results via queued signals (`Qt::QueuedConnection`).
  - `live_preview_manager.*` - decodes off-UI via dedicated `m_decodePool`, uses `QCache` (LRU ~512MB), delivers via signals. Call `invalidate(filePath)` when assets change.
  - `importer.*` - batches DB inserts in transactions, emits progress (currently main-thread DB)
  - `file_ops.*` - `FileOpsQueue` singleton runs copy/move/delete in background with progress signals
- **DB**: SQLite via QtSql (`db.{h,cpp}`). Single-threaded connection - never share QSqlDatabase/QSqlQuery across threads. Use transactions for bulk ops; prefer prepared statements. `IAssetDatabase` interface in `i_asset_database.h` for testability.
- **Logging**: Single handler in `main.cpp` -> `LogManager`. Use `qDebug/qWarning/qCritical`; don't add handlers.
- **Data path**: `QStandardPaths::AppDataLocation` (`%AppData%/KAsset/...`). Legacy migration in `main.cpp`.

### Media backends
| Purpose | Backend | Guard macro |
|---------|---------|-------------|
| Video/sequence playback | GStreamer | `HAVE_GSTREAMER` |
| Still images | OpenImageIO | `HAVE_OPENIMAGEIO` |
| Conversion only | FFmpeg | `HAVE_FFMPEG` |
| PDF viewing | Qt PDF | `HAVE_QT_PDF[_WIDGETS]` |

Guard optional features with both defined check and value:
```cpp
#if defined(HAVE_OPENIMAGEIO) && HAVE_OPENIMAGEIO
    // OpenImageIO-specific code
#endif
```

### Build (Windows)
```powershell
# Portable app (for development/testing)
scripts/build-windows.ps1 -Generator Ninja -Package
# Output: dist/portable/bin/kassetmanagerqt.exe

# Final installer (requires NSIS)
scripts/build-installer.ps1
# Output: installer/KAssetManager-Setup.exe (runs build-windows.ps1 first unless -SkipBuild)
```
- Auto-detects `VCPKG_ROOT`, `FFMPEG_ROOT`, `IMAGEMAGICK_ROOT`, bundled GStreamer in `third_party/gstreamer`
- CMake options: `BUILD_APP`, `BUILD_TESTS`, `ENABLE_CLANG_TIDY`, `ENABLE_ASAN`, `ENABLE_UBSAN`, `ENABLE_COVERAGE`
- Requires MSVC (2019/2022) - the build script explicitly prefers MSVC over MinGW

### Tests
- Framework: QtTest (`native/qt6/tests/`)
- Pattern: `QTemporaryDir` for test DB/files, `QVERIFY`/`QCOMPARE` assertions, `QSignalSpy` for async
- Run: `ctest --test-dir native/qt6/build/ninja --output-on-failure` after build
- Some tests compile with `HAVE_OPENIMAGEIO=0`/`HAVE_FFMPEG=0` to skip heavy deps

### Key patterns
- **Models**: `AssetsModel` exposes roles (`IdRole`, `FilePathRole`, `IsSequenceRole`, etc.), debounces reloads via `QTimer` (100ms). Use `data(index, SomeRole)` to access properties.
- **Sequences**: `sequence_detector.*` handles image sequence detection, gap analysis, and version tracking. Sequences stored as single DB rows with frame range metadata. Use `SequenceDetector::toHashPatternPath()` for pattern conversion.
- **Annotations**: `annotation_layer.*` and `annotation_items.*` provide frame-accurate drawing tools (pen, text, shapes, arrows) with per-frame storage and export. Uses `QUndoStack` for undo/redo. Draw modes: `None`, `Select`, `Pen`, `Text`, `Rectangle`, `Ellipse`, `Arrow`.
- **Project folders**: `project_folder_watcher.*` monitors disk folders via `QFileSystemWatcher` with debounced refresh. `ProjectImportController` handles auto-import of new files.
- **Drag-and-drop**: Internal MIME `application/x-kasset-asset-ids`; external uses `text/uri-list` + folder paths for sequences. See `drag_utils.*` for Explorer interop.
- **File ops**: Use `FileOpsQueue::instance()` singleton for copy/move/delete; never block UI with file I/O.
- **Everything SDK**: Fast disk search via `everything_search*.{h,cpp}`, DLL in `third_party/everything/`

### SQL patterns
- Always use prepared statements with `q.prepare()` + `q.addBindValue()` - never string interpolation
- For bulk IN clauses, build placeholder string dynamically:
  ```cpp
  QStringList marks; marks.reserve(ids.size());
  for (int i = 0; i < ids.size(); ++i) marks << "?";
  q.prepare(QString("SELECT ... WHERE id IN (%1)").arg(marks.join(',')));
  for (int id : ids) q.addBindValue(id);
  ```
- DB class provides `prepared(key, sql)` for caching prepared statements

### File Manager performance
- **Tree expansion**: Folder-only - never trigger file enumeration, shell icon lookups, or metadata/preview work when expanding nodes
- **Preview/Info panes**: Only invoke `LivePreviewManager` and metadata readers on explicit selection AND when panes are visible
- **Lazy loading**: Decode only when items enter the viewport; use `FileOpsQueue` for shell operations

### Threading rules
1. Never call `QApplication::processEvents()` - use signals/slots with queued connections
2. DB operations must stay on the thread that opened the connection (currently main thread)
3. For new async work: `QtConcurrent::run([...]{ ... })` then emit signal back to UI
4. Use `QMutex`/`QReadWriteLock` when sharing data between decode pool and UI

### Adding features
1. Wire UI via `MainWindow` and existing models
2. Long work -> `QtConcurrent::run`, post back via signals
3. New optional deps -> add CMake detection + `HAVE_*` guard
4. Reuse packaging hooks in `CMakeLists.txt` for DLL deployment

### Key files
- Architecture: `docs/ARCHITECTURE.md`, `docs/DEVELOPER_GUIDE.md`
- Build: `native/qt6/CMakeLists.txt`, `scripts/build-windows.ps1`
- Core: `src/main.cpp`, `src/db.{h,cpp}`, `src/mainwindow.{h,cpp}`
- Models: `src/assets_model.cpp`, `src/virtual_folder_tree_model.cpp`
- Media: `src/live_preview_manager.{h,cpp}`, `src/sequence_detector.{h,cpp}`
