# Tasks  Review Remediation Backlog

Updated: 2025-11-02
Source: CODE_REVIEW_REPORT.md (v0.2.0)

Legend: [x] todo, [/] in progress, [x] done

## Now (Phase 1  Immediate)
- [x] H-1 Remove orphaned disk thumbnail system
  - [x] Delete native/qt6/src/thumbnail_generator.cpp
  - [x] Delete native/qt6/src/thumbnail_generator.h
  - [x] Grep repo for "thumbnail_generator" and remove mentions (includes/comments)
  - [x] Update PERFORMANCE_OPTIMIZATIONS.md to LivePreview only
  - [x] Update API_REFERENCE.md to remove ThumbnailGenerator docs
  - DoD: No references; builds & packages succeed

- [x] H-1 Rename misleading slots in MainWindow to Preview terminology (and wire-up)
  - [x] mainwindow.h/.cpp: onGenerateThumbnailsForFolder > onPrefetchLivePreviewsForFolder
  - [x] mainwindow.h/.cpp: onRegenerateThumbnailsForFolder > onRefreshLivePreviewsForFolder
  - [x] mainwindow.h/.cpp: onGenerateThumbnailsRecursive > onPrefetchLivePreviewsRecursive
  - [x] mainwindow.h/.cpp: onRegenerateThumbnailsRecursive > onRefreshLivePreviewsRecursive
  - [x] Update connects/actions/menu strings/tooltips/QSettings keys (if any)
  - DoD: Compile passes; UI strings standardized to "Preview"; behavior unchanged

- [/] H-4 Reduce/replace qDebug() occurrences
  - [x] Adopt policy: use qInfo/qWarning/qCritical or LogManager; dev-only under #ifdef QT_DEBUG
  - [x] Replace/remove high-traffic qDebug in mainwindow.cpp and other hot paths
  - [/] Aim: reduce from 216 to <50; currently at 155 (28% reduction); keep diagnostics gated by env or DEBUG
  - DoD: Build clean; app log readable without spam

- [x] M-1 Fix potential ownership leaks
  - [x] assets_model.cpp: audit raw new; ensure QObject parents / smart pointers (mimeData() correctly unparented)
  - [x] drag_utils.cpp: audit/fix (QMimeData now parented to QDrag)
  - [x] file_ops_dialog.cpp: audit/fix (QHBoxLayout properly managed)
  - [x] import_progress_dialog.cpp: audit/fix (QHBoxLayout properly managed)
  - [x] log_viewer_widget.cpp: audit/fix (all widgets already have proper parents)
  - DoD: No raw new without parent/smart pointer in these files; spot-run app and exit cleanly âœ“

- [x] L-1/L-4 Documentation updates
  - [x] live_preview_manager.h: brief Doxygen about thread safety and mutex
  - [x] PERFORMANCE_OPTIMIZATIONS.md: replace disk-thumbnails sections with LivePreview (done in Phase 1.1)
  - DoD: Docs reflect current architecture âœ“

- [x] Phase 1.6 Verify Phase 1 with full build and package
  - [x] Run full build: powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
  - [x] Verify package created: dist/portable/bin/kassetmanagerqt.exe
  - [x] Manual smoke test: import folder, browse grid/list, preview, log viewer
  - [x] Verify no crashes on exit
  - DoD: Build succeeds; package created; no crashes in manual testing âœ“

## Completed (Phase 2  Complete)
- [x] H-3 Establish Qt Test harness and seed tests
  - [x] Add tests/ CMake target and runner; integrate optional ctest
  - [x] Create tests/test_db.cpp: createFolder, upsertAsset, transactions
  - [x] Add basic model/importer tests
  - DoD: Tests compile and run locally; green

- [x] M-6 Make LivePreview cache size configurable
  - [x] Add QSettings key and Settings dialog control
  - [x] Apply at runtime; add bounds; log metrics under KASSET_DIAGNOSTICS
  - DoD: Persisted and effective setting

- [x] M-4 Standardize file existence validation
  - [x] Add utility helper; replace inconsistent checks
  - DoD: All external path ops validated with consistent helper; early returns + clear logs

- [x] M-5 DB index visibility for assets.file_path
  - [x] Decide: document implicit UNIQUE index or add explicit index
  - [x] If adding: migration/block in DB init; verify with EXPLAIN
  - [x] Update TECH.md with decision
  - DoD: Decision implemented & documented

- [x] M-7 Centralize sequence detection regex
  - [x] Move pattern to SequenceDetector (single source)
  - [x] Update mainwindow.cpp and other callers
  - [x] Add unit tests for common patterns
  - DoD: One authoritative definition; tests pass

## Later (Phase 3  quarter)
- [x] H-2 Extract FileManagerWidget (+controller); remove direct DB from MainWindow FM flows
  - DoD: MainWindow shrinks; FM logic isolated and tested

- [x] H-2 Extract AssetManagerWidget (+controller)
  - DoD: MainWindow < 1,000 LOC; orchestrator only

- [x] H-3 Raise test coverage to targets (DB 80%, models 70%, importer 50%)
  - DoD: Coverage report hits targets locally; green when CI enabled

- [x] L-2/L-3 Naming + constants polish
  - [x] Extract magic numbers to named constants where appropriate
  - [x] Standardize on "Preview" (
    rename internal identifiers/strings; avoid "Thumb/Thumbnail")
  - DoD: Consistent naming; rationale documented for key constants

## Verification steps (run for each completed bullet group)
- Build & package: powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
- Manual smoke: import folder, browse grid/list, preview, log viewer; ensure no crashes
- If tests present: execute test runner/ctest and confirm green

## Latest updates
- 2025-11-02: Phase 2 complete. All 5 Phase 2 tasks finished. Ready to merge to main.

