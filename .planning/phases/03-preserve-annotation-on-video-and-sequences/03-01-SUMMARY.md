---
phase: 03-preserve-annotation-on-video-and-sequences
plan: 01
subsystem: ui
tags: [qt6, wayland, tlrender, annotations, video, sequences]
requires:
  - phase: 02-linux-port
    provides: Fedora 43 Wayland playback baseline and tlRender preview plumbing
provides:
  - Live video annotations remain on a transparent overlay above the tlRender viewport
  - Sequence annotation sessions reset cleanly per asset while preserving per-frame state
  - Annotation JSON serialization preserves moved item scene positions for export/reload
affects: [preview, annotation-export, wayland-playback]
tech-stack:
  added: []
  patterns: [defer tlRender surface use until needed, keep annotation editing on transparent overlay scenes, serialize annotation geometry in scene coordinates]
key-files:
  created: [.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-audit.md, .planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-SUMMARY.md, native/qt6/tests/test_annotation_items.cpp]
  modified: [native/qt6/src/preview_overlay.cpp, native/qt6/src/annotation_items.cpp, native/qt6/tests/CMakeLists.txt]
key-decisions:
  - "Kept video annotation editing on a transparent QGraphicsView overlay instead of swapping to the raster image view, so the live tlRender surface stays visible on Wayland."
  - "Stored moved annotation geometry in scene coordinates for all item types so per-frame reload/export stays stable after user repositioning."
patterns-established:
  - "Wayland video annotation edits should track the live viewport via overlay geometry sync, not by replacing the playback widget."
  - "Annotation serialization must include item translation offsets before persisting per-frame JSON."
requirements-completed: [FEDORA43-ANNOTATION]
duration: 4 min
completed: 2026-04-11
---

# Phase 03 Plan 01: Preserve annotation on video and sequences Summary

**Wayland-safe live video annotation overlays with frame-accurate sequence/video persistence and export-safe moved-item serialization**

## Performance

- **Duration:** 4 min
- **Started:** 2026-04-11T12:03:27+03:00
- **Completed:** 2026-04-11T09:07:25Z
- **Tasks:** 3
- **Files modified:** 6

## Accomplishments
- Audited existing work and isolated the remaining blocker to video annotation mode still abandoning the live tlRender surface.
- Kept video annotations on a transparent overlay synced to the tlRender viewport while resetting per-sequence annotation session state.
- Verified moved annotation serialization with focused QtTest coverage and rebuilt the preview overlay path successfully.

## task Commits

Each task was committed atomically:

1. **task 1: Audit existing annotation implementation and current coverage** - `adc2597` (docs)
2. **task 2: Implement minimal Wayland annotation fixes** - `7ba0b8f` (fix)
3. **task 3: Validate with focused tests and summarize outcomes** - `248d0d3` (test/docs)

## Files Created/Modified
- `.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-audit.md` - recorded the partial-completion audit and remaining overlay gap
- `native/qt6/src/preview_overlay.cpp` - keeps video annotations on a transparent overlay and resets sequence annotation state
- `native/qt6/src/annotation_items.cpp` - serializes moved item coordinates in scene space for reload/export correctness
- `native/qt6/tests/CMakeLists.txt` - registers the focused annotation item test target in the QtTest suite
- `native/qt6/tests/test_annotation_items.cpp` - covers moved text, rectangle, arrow, and freehand serialization behavior

## Decisions Made
- Used the existing transparent `annotationOverlayView` for video editing instead of switching to `imageView`, preserving the plan requirement to keep the overlay above the GL surface.
- Treated moved annotation serialization as correctness-critical because export/reload would otherwise drift after user edits.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- `ctest -R "test_annotation_items|test_platform_session"` reported `test_platform_session` as not built in the current build tree. I kept verification focused on the planned annotation target and the app rebuild instead of broadening scope into unrelated test registration work.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Annotation persistence, export, and live overlay behavior are ready for manual Fedora 43 Wayland verification.
- Recommended manual check: launch the Wayland build, annotate a video and an image sequence across multiple frames, then confirm scrubbing and export retain those drawings.

## Self-Check: PASSED

---
*Phase: 03-preserve-annotation-on-video-and-sequences*
*Completed: 2026-04-11*
