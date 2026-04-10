# Codebase Concerns

**Analysis Date:** 2026-04-11

## Tech Debt

**Monolithic UI shell:**
- Issue: Core UI behavior, file management, project-manager actions, preview launching, and external-process wiring are concentrated in very large translation units.
- Files: `native/qt6/src/mainwindow.cpp`, `native/qt6/src/mainwindow.h`, `native/qt6/src/preview_overlay.cpp`, `native/qt6/src/preview_overlay.h`
- Impact: Small UI changes have a large regression surface, compile times stay high, and behavior is difficult to isolate for testing or refactoring.
- Fix approach: Split window orchestration into focused controllers/widgets, move file/process helpers behind services, and keep preview/export logic out of the top-level window classes.

**Duplicated SQLite schema and import logic:**
- Issue: Main-library and project-manager data layers implement similar schema creation and asset upsert logic separately.
- Files: `native/qt6/src/db.cpp`, `native/qt6/src/db.h`, `native/qt6/src/project_db.cpp`, `native/qt6/src/project_db.h`, `native/qt6/src/project_import_worker.cpp`
- Impact: Schema changes can drift between databases, bug fixes have to be repeated, and project-manager behavior can diverge from asset-library behavior.
- Fix approach: Centralize shared schema fragments and shared upsert helpers, then keep only project-specific tables and flows in `project_db`.

**Platform-specific behavior embedded in general UI code:**
- Issue: Windows shell integration, Everything SDK loading, and ffprobe path assumptions are implemented inline in app logic instead of isolated adapters.
- Files: `native/qt6/src/mainwindow.cpp`, `native/qt6/src/everything_search.cpp`, `native/qt6/src/media_converter_worker.cpp`
- Impact: Linux/Wayland work and cross-platform maintenance require touching unrelated UI code, increasing regression risk.
- Fix approach: Move shell, search-provider, and media-tool discovery behind platform adapter classes with one implementation per OS.

## Known Bugs

**Everything search results never mark assets as already imported:**
- Symptoms: Search results from the Everything integration report `isImported = false` even when the asset already exists in the application database.
- Files: `native/qt6/src/everything_search.cpp`
- Trigger: Run `EverythingSearch::search()` on Windows for files that are already present in the asset library.
- Workaround: Treat the imported-state indicator as unreliable; confirm import status from the main asset views instead.

**Case-sensitive paths are collapsed during import on Linux:**
- Symptoms: Distinct directories or files whose paths differ only by case can map to the same normalized key during import.
- Files: `native/qt6/src/importer.cpp`, `native/qt6/src/project_import_worker.cpp`
- Trigger: Import a tree on a case-sensitive filesystem where sibling paths differ only by letter case.
- Workaround: Avoid importing folder trees with case-only path differences until path normalization stops lowercasing Linux paths.

**Project-manager reimport can churn asset IDs and drop linked state:**
- Symptoms: Reimported project assets can be deleted and recreated instead of updated in place, which risks losing relations that point at `assets.id`.
- Files: `native/qt6/src/project_import_worker.cpp`, `native/qt6/src/project_db.cpp`
- Trigger: Reimport project files that already exist in the project database; `INSERT OR REPLACE` on `assets.file_path` can replace the row.
- Workaround: Avoid depending on stable project-manager asset IDs across repeated imports until replace-based upserts are converted to update-in-place logic.

## Security Considerations

**Windows DLL hijack surface in Everything SDK loading:**
- Risk: The app loads `Everything64.dll` from the application directory, current directory, and PATH-like locations before verifying provenance.
- Files: `native/qt6/src/everything_search.cpp`
- Current mitigation: The feature is optional and the code refuses to enable it when required exports or the Everything service are unavailable.
- Recommendations: Load only from a trusted bundled path, verify architecture/signature where possible, and avoid probing the current working directory.

**Crash and runtime logs expose local file-system details:**
- Risk: Logs and crash artifacts include absolute paths and operational details in user data locations.
- Files: `native/qt6/src/main.cpp`, `native/qt6/src/log_manager.cpp`, `native/qt6/src/everything_search.cpp`, `native/qt6/src/thumbnail_cache_manager.cpp`
- Current mitigation: Release crash logging in `native/qt6/src/main.cpp` avoids raw exception addresses.
- Recommendations: Reduce path-heavy debug logging in release builds, add rotation/retention rules for logs, and make diagnostic logging opt-in for sensitive environments.

**Security process documentation is stale relative to the shipped app version:**
- Risk: The published security policy only lists version `1.4.0` as supported while the Qt app project version is `1.8.5`.
- Files: `SECURITY.md`, `native/qt6/CMakeLists.txt`
- Current mitigation: A reporting contact exists in `SECURITY.md`.
- Recommendations: Update supported-version policy to match current releases so users know which builds receive fixes.

## Performance Bottlenecks

**Blocking sleeps on the GUI thread during annotated frame export:**
- Problem: Exporting annotated video frames pauses the player and blocks the UI with fixed `QThread::msleep(100)` delays per frame.
- Files: `native/qt6/src/preview_overlay.cpp`
- Cause: Frame seeking and capture are synchronized with sleep-based waits instead of async callbacks or worker-driven capture.
- Improvement path: Move frame export into a worker pipeline and wait on player/frame-ready signals instead of sleeping on the UI thread.

**Importer pumps the event loop while doing bulk database work:**
- Problem: Large imports call `QApplication::processEvents()` repeatedly inside import loops.
- Files: `native/qt6/src/importer.cpp`
- Cause: The synchronous importer keeps work on the UI thread and uses manual event pumping to keep the dialog responsive.
- Improvement path: Run bulk import work off the UI thread, emit throttled progress signals, and remove re-entrant event-loop pumping.

**Folder-model refresh waits synchronously for concurrent tasks to finish:**
- Problem: Model destruction and refresh paths call `waitForFinished()` on outstanding watchers.
- Files: `native/qt6/src/everything_folder_model.cpp`
- Cause: Cancellation is followed by synchronous waiting on the calling thread.
- Improvement path: Use non-blocking cancellation cleanup and defer destructive model resets until watcher completion signals arrive.

**Media conversion repeatedly spawns blocking ffprobe processes per item:**
- Problem: Duration, dimensions, and FPS probing launch separate `ffprobe` processes with blocking `waitForFinished()` calls.
- Files: `native/qt6/src/media_converter_worker.cpp`
- Cause: Metadata probing is serialized through short-lived subprocesses for each task.
- Improvement path: Cache probe results per source, collapse metadata queries into a single ffprobe invocation, and keep tool discovery platform-neutral.

## Fragile Areas

**Thumbnail cache API silently fails outside the UI thread:**
- Files: `native/qt6/src/thumbnail_cache_manager.cpp`, `native/qt6/src/live_preview_manager.cpp`
- Why fragile: `ThumbnailCacheManager::getCachedThumbnail()` returns a null pixmap when called off the GUI thread. Accidental misuse degrades behavior without a hard failure in release flows.
- Safe modification: Keep all `QPixmap` cache reads on the UI thread and pass `QImage` across worker boundaries.
- Test coverage: `native/qt6/tests/test_live_preview_manager.cpp` covers the happy-path request/cache flow, but there is no test that guards against thread-affinity regressions.

**Project import worker owns its own SQLite connection lifecycle:**
- Files: `native/qt6/src/project_import_worker.cpp`
- Why fragile: Thread shutdown, cancellation, and `QSqlDatabase::removeDatabase()` sequencing are tightly coupled to object lifetime. Reordering cleanup can create hard-to-reproduce shutdown issues.
- Safe modification: Keep DB connection open/close entirely inside the worker thread, and change cancellation/cleanup behavior only with targeted thread-lifecycle tests.
- Test coverage: No dedicated tests cover `ProjectImportWorker` cancellation, repeated starts, or reimport behavior.

**Recursive file deletion is homegrown and destructive:**
- Files: `native/qt6/src/file_ops.cpp`, `native/qt6/src/file_ops.h`
- Why fragile: Permanent delete and recursive removal are implemented manually, so edge cases like permissions, partial failures, symlink handling, and cancellation semantics are easy to break.
- Safe modification: Change recursive delete behavior only with filesystem integration tests that exercise nested directories, read-only files, and cancellation.
- Test coverage: No tests target `FileOpsQueue` or `removeRecursively()`.

## Scaling Limits

**Everything-backed folder browsing is hard-capped per query:**
- Current capacity: `kMaxEverythingResults = 65535`.
- Limit: Trees or searches that exceed that count can truncate folder discovery.
- Scaling path: Add paging or iterative fetching in `native/qt6/src/everything_folder_model.cpp` instead of relying on a single bounded result set.

**Core library database is effectively single-writer application state:**
- Current capacity: One unnamed `QSQLITE` connection in `native/qt6/src/db.cpp` backs the main asset library, while long-running project imports create separate ad hoc connections.
- Limit: More background write paths increase contention risk and make thread ownership harder to reason about.
- Scaling path: Introduce a dedicated DB worker/queue for the main library and standardize one connection-per-thread rules across all background flows.

## Dependencies at Risk

**tlRender:**
- Risk: Application configure/build fails hard when tlRender is not present.
- Impact: Fresh developer setup and CI portability depend on an external prebuilt tree existing in expected locations.
- Migration plan: Keep tlRender as the playback backend, but encapsulate discovery and add a clearer bootstrap path or optional degraded build mode.

**Everything SDK / Everything service:**
- Risk: Windows search functionality depends on a separately deployed DLL and a running external indexing service.
- Impact: Search features degrade to unavailable with runtime warnings instead of a built-in fallback.
- Migration plan: Keep the integration optional, but provide a native filesystem fallback or a packaged/sandboxed provider adapter.

## Missing Critical Features

**High-value integration coverage for destructive and UI-heavy workflows:**
- Problem: The codebase has unit tests for lower-level pieces, but it does not have automated coverage for preview/export workflows, file operations, and project-manager import orchestration.
- Blocks: Safe refactoring of `native/qt6/src/mainwindow.cpp`, `native/qt6/src/preview_overlay.cpp`, `native/qt6/src/project_import_worker.cpp`, and `native/qt6/src/file_ops.cpp`.

## Test Coverage Gaps

**Main UI shell and preview/export path:**
- What's not tested: Context-menu actions, preview launching, annotation export, external-process launch points, and preview lock release behavior.
- Files: `native/qt6/src/mainwindow.cpp`, `native/qt6/src/preview_overlay.cpp`, `native/qt6/src/media_convert_dialog.cpp`
- Risk: UI regressions and platform-specific breakage can ship unnoticed.
- Priority: High

**Project-manager database and threaded import path:**
- What's not tested: `ProjectDB` schema behavior, reimport semantics, notification retention, worker cancellation, and thread cleanup.
- Files: `native/qt6/src/project_db.cpp`, `native/qt6/src/project_import_worker.cpp`
- Risk: Data loss or duplicate/corrupt project state can appear during large imports.
- Priority: High

**Filesystem mutation queue:**
- What's not tested: Recursive delete, copy/move cancellation, conflict behavior, and partial-failure recovery.
- Files: `native/qt6/src/file_ops.cpp`, `native/qt6/src/file_ops.h`
- Risk: Destructive operations can fail in edge cases without automated detection.
- Priority: High

**Everything folder model concurrency:**
- What's not tested: Refresh-time cancellation, watcher cleanup, truncation behavior, and large result handling.
- Files: `native/qt6/src/everything_folder_model.cpp`, `native/qt6/src/everything_search.cpp`
- Risk: UI stalls and inconsistent tree state can regress unnoticed on Windows.
- Priority: Medium

---

*Concerns audit: 2026-04-11*
