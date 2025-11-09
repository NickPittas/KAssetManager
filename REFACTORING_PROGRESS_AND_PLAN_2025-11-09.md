# MainWindow Refactoring — Progress Report and Plan (2025-11-09)

This document complements MAINWINDOW_REFACTORING_PLAN.md (detailed strategy) and REFACTORING_SUMMARY.md (executive overview). It captures what has been completed so far, what is in progress, and the precise next steps to continue tomorrow without context switching.

---

## Current Status Snapshot

- Branch: refactor/mainwindow-filemanager-migration
- Build status: OK (portable + CPack artifacts produced)
- Behavioral focus: Preserve original UX and parity; no feature changes during refactors

### Highlights since last plan update
- File Manager extracted into a dedicated widget and integrated into MainWindow
- Preview subsystems hardened (FM preview pane and PreviewOverlay) with multiple regression fixes
- Helper classes and UI utilities extracted to dedicated compilation units

---

## What Has Been Migrated (Done)

### Phase 1 — Helper classes (Completed)
Extracted out of mainwindow.cpp into their own files:
- widgets/sequence_grouping_proxy_model.h/.cpp
- widgets/asset_grid_view.h/.cpp
- widgets/fm_icon_provider.h/.cpp
- widgets/asset_item_delegate.h/.cpp
- widgets/fm_item_delegate.h/.cpp
- widgets/grid_scrub_overlay.h/.cpp
- widgets/grid_scrub_controller.h/.cpp

Benefits:
- Reduces MainWindow size and compile times
- Clearer ownership and testability of each helper

### Phase 2 (partial) — File Manager subsystem
- FileManagerWidget created and integrated in MainWindow setup:
  - native/qt6/src/widgets/file_manager_widget.h
  - native/qt6/src/widgets/file_manager_widget_rebuilt.cpp (currently compiled)
- CMake points to rebuilt unit:
  - native/qt6/CMakeLists.txt → src/widgets/file_manager_widget_rebuilt.cpp
- MainWindow now instantiates and wires FileManagerWidget (search: "FileManagerWidget" shows creation and signal/slot hookups in setupFileManagerUi())
- Many file operations, preview wiring, navigation, and UI controls run through FileManagerWidget

### Preview and Playback improvements (Delivered, behavior preserved)
- Pan gesture no longer interferes with drag/drop in preview panes; DnD disabled in preview contexts
- PreviewOverlay controls anchored to the bottom; no content cropping; resizable window instead of fullscreen child
- Image sequence preview opens instantly; async frame loading with epoch-based cancellation; cache bar separated from slider
- Video playback moved to QGraphicsVideoItem (zoom/pan unified with images); fit-to-view logic governed by explicit flag; zoom/pan preserved during playback
- Crash on video open fixed by preserving QGraphicsVideoItem across scene clears (remove before clear, re-add afterward)
- HDR detection expanded for sequences/stills (EXR/HDR/TIF/TIFF/PSD) with color space dropdown shown only when relevant
- Removed stray/star rating widget from overlay if present

---

## What’s Left (Actionable)

### A) Complete Phase 2 — Finish File Manager extraction and dedupe (Top priority)
- Move remaining onFm* slots and FM preview helpers still in MainWindow into FileManagerWidget
  - Examples in native/qt6/src/mainwindow.cpp include:
    - onFmSelectionChanged, onFmTogglePreview, onFmOpenOverlay, navigation handlers, sequence helpers
- Remove legacy/duplicate FileManagerWidget variants and keep a single canonical implementation
  - Current files: file_manager_widget.cpp, file_manager_widget_fixed.cpp, file_manager_widget_rebuilt.cpp (CMake includes the rebuilt variant)
  - Target: consolidate into file_manager_widget.cpp + header; delete unused variants and update CMake
- Sweep MainWindow for fm* members and FM-only state; relocate into FileManagerWidget and expose a minimal public API
- Verify all FM shortcuts, context menus, and OS-integrated file ops continue to work (Explorer/Shell handlers, Recycle Bin)

### B) Phase 3 — Asset Manager UI Builder (when A is complete)
- Extract asset-manager page construction into ui/asset_manager_ui_builder.{h,cpp}
- Replace MainWindow::setupUi UI-construction block with builder call to reduce surface area

### C) Phase 4 — Tag Manager (optional next)
- Extract tag operations into components/tag_manager.{h,cpp}
- Connect via signals; maintain identical behavior and UI

### D) Cleanups and parity checks
- Ensure PreviewOverlay creation is consistent and top-level in all code paths (already standardized) and remains parentless for stability
- Confirm no DnD is accepted/initiated in preview panes across both File Manager and Asset Manager
- Confirm FM preview pane timeline layout and controls match Explorer-like guidelines (full-width slider on top; buttons beneath)

---

## Tomorrow’s Concrete Plan (Checklists)

1) Finish FM extraction and dedupe (approx. 2–3 hrs)
- [ ] Move onFmSelectionChanged, onFmTogglePreview, onFmOpenOverlay, navigation handlers, and sequence helpers into FileManagerWidget
- [ ] Replace any direct fm* member access from MainWindow with FileManagerWidget API calls
- [ ] Ensure fmPreviewInfoSplitter and preview-pane widgets are owned by FileManagerWidget
- [ ] Re-run build/package; fix compile errors

2) Consolidate FileManagerWidget implementation (approx. 45–60 min)
- [ ] Merge file_manager_widget_rebuilt.cpp → file_manager_widget.cpp
- [ ] Remove file_manager_widget_fixed.cpp and any unused duplicates
- [ ] Update native/qt6/CMakeLists.txt to reference file_manager_widget.cpp only
- [ ] Build/package to validate

3) Parity sweep + verification (approx. 45–60 min)
- [ ] End-to-end FM feature test pass (tree navigation, Grid/List, file ops, favorites, context menus, preview info)
- [ ] Verify DnD policies for File Manager and Asset Manager match requirements (Explorer vs Nuke/AE)
- [ ] Verify PreviewOverlay for videos and sequences from both AM + FM
- [ ] Persisted settings (QSettings) still work: FileManager/[StateName] keys

4) Prep for Phase 3 (approx. 30–45 min)
- [ ] Isolate Asset Manager setupUi block into a builder skeleton (no behavior changes)
- [ ] Identify all AM components to return in builder::Components struct

---

## Risks and Guardrails
- Do not change runtime behavior during extraction; strictly move code
- Remove old code only after verifying the new path is fully wired and tested
- Keep using OS handlers for file ops (Explorer/Shell APIs) and FileOpsQueue for async
- Preview system: maintain unified zoom/pan across media types; never re-enable DnD in preview panes

---

## How to Validate
- Build (per team guideline):
  - powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
- Quick smoke checks:
  - Open FM and AM, play video and image sequences in overlay and pane
  - Check cache bar updates for sequences, and timeline slider layout
  - Verify copy/cut/paste/delete/rename/new folder/favorites/context menus

---

## Pointers for Fast Resumption
- MainWindow integration points with FileManagerWidget:
  - native/qt6/src/mainwindow.cpp: setupFileManagerUi() creates FileManagerWidget and connects signals
  - native/qt6/src/mainwindow.h: FileManagerWidget* fileManagerWidget member
- CMake linkage:
  - native/qt6/CMakeLists.txt → src/widgets/file_manager_widget_rebuilt.cpp (to be consolidated)
- PreviewOverlay changes live in:
  - native/qt6/src/preview_overlay.{h,cpp}

---

## Open Questions (to confirm before Phase 3)
- Any remaining AM-specific preview behaviors that must be kept inside MainWindow instead of a builder?
- Preferred final filename convention for the canonical FileManagerWidget source after consolidation?

---

Prepared by: Augment Agent (GPT‑5)
Updated: 2025-11-09

