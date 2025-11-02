# Remediation Plan – Addressing CODE_REVIEW_REPORT.md (v0.2.0)

Generated: 2025-11-02
Scope: Implement a focused, low‑risk sequence of changes to resolve issues in CODE_REVIEW_REPORT.md while keeping builds green and packaging intact.

## Guiding principles
- Remove dead/duplicate code and misleading names (no placeholders; fully implement or delete)
- Keep CMake/build scripts succeeding (Windows Qt 6; scripts/build-windows.ps1 with -Package)
- Respect Qt patterns: parent-child ownership; models handle DB; MainWindow orchestrates only
- Minimal code, clear naming (standardize on “Preview” instead of “Thumb/Thumbnail”)
- Verify after each milestone: build + manual checklist; add automated tests where feasible

## Phased plan (with mapping to review items)

### Phase 1 – Immediate safety and clarity (1 week)
1. Remove orphaned disk thumbnail system [H‑1]
   - Delete native/qt6/src/thumbnail_generator.{cpp,h}
   - Search & remove any remaining references (includes, commented code, docs, logs)
   - Update PERFORMANCE_OPTIMIZATIONS.md to reflect LivePreview only (see L‑4)
   - Acceptance: No references/build includes; docs updated; app builds & packages

2. Rename misleading "thumbnail" slots to preview terms [H‑1, L‑3]
   - mainwindow.{h,cpp}:
     - onGenerateThumbnailsForFolder → onPrefetchLivePreviewsForFolder
     - onRegenerateThumbnailsForFolder → onRefreshLivePreviewsForFolder
     - onGenerateThumbnailsRecursive → onPrefetchLivePreviewsRecursive
     - onRegenerateThumbnailsRecursive → onRefreshLivePreviewsRecursive
   - Update connects, actions, menus, tooltips, and any QSettings keys if applicable
   - Acceptance: All references compile; UI strings use “Preview”; behavior unchanged

3. Reduce/replace qDebug() usage [H‑4]
   - Policy: production code uses qInfo/qWarning/qCritical or LogManager; dev‑only messages under #ifdef QT_DEBUG
   - Apply targeted replacements/removals to cut 216 qDebug to <50, prioritize hot paths and user‑visible log spam
   - Acceptance: Build clean; app log remains informative without debug noise

4. Fix potential memory leaks by enforcing ownership [M‑1]
   - Audit files listed (assets_model.cpp, drag_utils.cpp, file_ops_dialog.cpp, import_progress_dialog.cpp, log_viewer_widget.cpp) and any others; ensure QObject parents or smart pointers for non‑QObject
   - Acceptance: No raw new without parent/smart pointer; manual spot‑check via run and quit to ensure no leak warnings

5. Documentation sync [L‑4, L‑1]
   - PERFORMANCE_OPTIMIZATIONS.md: replace disk thumbnail sections with LivePreview design
   - Add short Doxygen block to live_preview_manager.h describing thread safety (mutex usage)
   - Acceptance: Docs reflect current architecture; headers document thread safety

### Phase 2 – Testing and small QoL improvements (2–3 weeks)
6. Establish Qt Test harness and first tests [H‑3]
   - Create tests/ with CMake target; add test_db.cpp (createFolder, upsertAsset, transactions)
   - Wire minimal CI locally (GitHub Actions workflow proposal kept in repo but not required to run yet)
   - Acceptance: `ctest` or test executable runs locally with passing tests; included in build script optionally

7. Configurable LivePreview cache size [M‑6]
   - Expose m_maxCacheEntries via QSettings + Settings dialog control; sane defaults and bounds
   - Log metrics (hit rate/evictions) to LogManager under KASSET_DIAGNOSTICS
   - Acceptance: Setting persists; changing value affects runtime cache behavior

8. Standardize external file existence checks [M‑4]
   - Add small helper in a shared utils header; replace inconsistent checks
   - Acceptance: All external file path operations validated consistently; early returns with clear logs

9. DB index documentation/explicit index on assets.file_path [M‑5]
   - Either document UNIQUE(file_path) implicit index or add explicit index; confirm with EXPLAIN
   - Acceptance: Decision recorded in TECH.md; schema updated or comment added

10. Centralize sequence detection regex [M‑7]
   - Move pattern to SequenceDetector (single source); update callers (mainwindow.cpp, sequence_detector.cpp)
   - Acceptance: One authoritative definition; tests for a few patterns

### Phase 3 – MainWindow de‑monolithization (quarter)
11. Extract FileManagerWidget [H‑2, M‑3]
   - Move File Manager UI + logic into its own widget; introduce FileManagerController for non‑UI logic
   - Acceptance: MainWindow shrinks; file ops routed via controller; no direct DB in MainWindow for FM flows

12. Extract AssetManagerWidget [H‑2]
   - Move Asset Manager UI + logic similarly; AssetsModel provides DB accessors; controller coordinates operations
   - Acceptance: MainWindow <1,000 LOC; acts as coordinator only

13. Increase automated test coverage [H‑3]
   - Add tests for models/importer/integration flows; target: DB 80%, models 70%, importer 50%
   - Acceptance: Coverage reports reach targets locally; green on CI when enabled

### Phase 4 – Longer‑term improvements
14. CI pipeline and quality gates
15. Naming cleanup and constants [L‑2, L‑3]
   - Extract magic numbers to named constants; standardize Preview naming across code/UI

## Cross‑cutting standards
- Logging: prefer LogManager; gate noisy logs under env var or QT_DEBUG
- Ownership: pass parent to QObject; use std::unique_ptr for non‑QObject
- DB access: only via models/controllers; no direct DB calls in MainWindow
- Naming: "Preview" only; no "Thumb/Thumbnail" in code/UI

## Verification per phase
- Build and package: scripts/build-windows.ps1 -Generator Ninja -Package
- Manual checklist: follow DEVELOPER_GUIDE.md steps; spot‑check critical flows (import, browse, preview)
- Tests: run new test targets (Phase 2+)

## Deliverables
- Phase 1: Cleaned codebase (no orphaned thumbnails), renamed API/UI, reduced debug logs, leak fixes, doc updates
- Phase 2: Test harness + initial tests, configurable preview cache, standardized checks/regex, DB index decision
- Phase 3: Separated widgets/controllers; MainWindow under 1,000 LOC; broader tests
- Phase 4: CI + polish (naming/constants)

## Risks & mitigations
- Large refactor risk (H‑2): mitigate with Phase 2 tests before Phase 3
- Behavior regressions from renames: verify signals/slots and menu actions with runtime checks
- Build/packaging stability: run full -Package build after each milestone

