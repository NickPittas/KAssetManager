---
phase: 03-preserve-annotation-on-video-and-sequences
verified: 2026-04-11T09:16:55Z
status: human_needed
score: 4/4 must-haves verified
human_verification:
  - test: "Wayland live video annotation overlay"
    expected: "On Fedora 43 Wayland, entering annotation mode keeps the live tlRender video visible and draws/editing occurs on the transparent overlay without blacking out or replacing the video surface."
    why_human: "Requires an actual Wayland GUI session and interactive rendering behavior that cannot be verified from static code inspection."
  - test: "Frame-accurate video/sequence annotation scrub and export"
    expected: "Annotations stay attached to the correct video/sequence frames while stepping and scrubbing, and exported annotated frames match the visible frame plus moved annotations."
    why_human: "Needs real media playback, interactive scrubbing, and exported image inspection under Wayland."
---

# Phase 3: Preserve Annotation On Video And Sequences Verification Report

**Phase Goal:** Preserve annotation overlay behavior, frame sync, and exports for both video and image sequences under Wayland.
**Verified:** 2026-04-11T09:16:55Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
| --- | --- | --- | --- |
| 1 | Video annotation mode keeps a transparent overlay above the tlRender viewport instead of replacing the live video surface. | ✓ VERIFIED | `preview_overlay.cpp:639-662` creates a transparent `annotationOverlayView`; `preview_overlay.cpp:4366-4393` binds `AnnotationLayer` to that overlay, syncs it to the active video widget, shows/raises it, and leaves video playback visible. |
| 2 | Video annotation state stays frame-synced during scrub/seek/frame-step. | ✓ VERIFIED | `preview_overlay.cpp:1862-2017` updates `lastKnownVideoFrame` during slider moves/releases and frame stepping, then calls `updateVideoAnnotationFrame()`; `preview_overlay.cpp:4872-4918` saves the old frame's annotations, captures the new frame, and loads annotations for the new frame. |
| 3 | Sequence annotation sessions reset per asset while preserving per-frame annotations inside a sequence. | ✓ VERIFIED | `preview_overlay.cpp:1510-1518` and `2796-2800` call `resetAnnotationSession()` when switching assets/sequences; `preview_overlay.cpp:2979-3099` saves the previous frame and reloads annotations for the current sequence frame. |
| 4 | Annotated exports use real frame pixels plus persisted annotation geometry, including moved items. | ✓ VERIFIED | `preview_overlay.cpp:4595-4680` exports stored annotated frames for sequence/video; `preview_overlay.cpp:4809-4846` composites `captureCurrentFrame()` with live annotation items; `annotation_items.cpp:177-196,307-325,367-378,420-431,511-523` serializes scene-space geometry; `test_annotation_items.cpp:15-70` verifies moved-item serialization/round-trip. |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
| --- | --- | --- | --- |
| `native/qt6/src/preview_overlay.cpp` | Wayland-safe overlay, frame sync, export flow | ✓ VERIFIED | Substantive implementation exists and is wired from `MainWindow` (`mainwindow.cpp:2527` and other call sites). |
| `native/qt6/src/annotation_layer.cpp` | Serialize/deserialize active annotation items | ✓ VERIFIED | `serializeAnnotations()` / `deserializeAnnotations()` bridge preview state to persisted JSON (`annotation_layer.cpp:278-297`). |
| `native/qt6/src/annotation_items.cpp` | Preserve moved annotation geometry in JSON | ✓ VERIFIED | Text, freehand, rectangle, ellipse, and arrow items serialize scene-space positions. |
| `native/qt6/tests/test_annotation_items.cpp` | Focused regression coverage for moved-item serialization | ✓ VERIFIED | Registered in `native/qt6/tests/CMakeLists.txt:253-269`; `ctest -R test_annotation_items` passes. |

### Key Link Verification

| From | To | Via | Status | Details |
| --- | --- | --- | --- | --- |
| `mainwindow.cpp` | `PreviewOverlay` | `new PreviewOverlay(this)` | ✓ WIRED | Overlay is instantiated from multiple main-window preview entry points. |
| `PreviewOverlay::enableAnnotationMode()` | Video overlay scene | `annotationLayer->setScene(annotationOverlayScene)` + `syncAnnotationOverlayToVideo()` | ✓ WIRED | Video annotations render on the transparent overlay, not the image scene. |
| `PreviewOverlay` | Per-frame annotation storage | `saveCurrentFrameAnnotations()` / `loadFrameAnnotations()` | ✓ WIRED | Sequence and video frame changes persist and reload JSON-backed frame annotations. |
| `AnnotationLayer` | `AnnotationItem` JSON | `item->toJson()` / `AnnotationItem::fromJson()` | ✓ WIRED | Export/reload path uses item serializers directly. |
| Export actions | Real frame pixels | `captureCurrentFrame()` / `m_player->getCurrentFrame()` / `originalPixmap` | ✓ WIRED | Export path composites live frame content with current annotation items. |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
| --- | --- | --- | --- | --- |
| `preview_overlay.cpp` video annotation path | `currentAnnotatedFrame`, `frameAnnotations` | `getVideoFrameNumber()` + `updateVideoAnnotationFrame()` + `m_player->getCurrentFrame()` | Yes | ✓ FLOWING |
| `preview_overlay.cpp` sequence annotation path | `currentAnnotatedFrame`, `frameAnnotations`, `originalPixmap` | `loadSequenceFrame()` + `saveCurrentFrameAnnotations()` + sequence frame files/cache | Yes | ✓ FLOWING |
| `exportAnnotatedFrame()` | `baseFrame` + `annotationLayer->annotations()` | `captureCurrentFrame()` + live scene items | Yes | ✓ FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
| --- | --- | --- | --- |
| Moved annotation geometry regression coverage | `ctest --test-dir build-linux --output-on-failure -R test_annotation_items` | `1/1 Test #15: test_annotation_items ... Passed` | ✓ PASS |
| Wayland live overlay rendering | N/A | Requires interactive Fedora 43 Wayland session | ? SKIP |
| End-to-end sequence/video overlay playback tests | `ctest --test-dir build-linux --output-on-failure -R test_platform_session` / `-R test_sequence_detector` | Test entries exist but executables were not present in the current build tree | ? SKIP |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
| --- | --- | --- | --- | --- |
| `FEDORA43-ANNOTATION` | None in `03-01-PLAN.md` frontmatter (**ORPHANED traceability**) | Annotation workflows work correctly on video and sequences under Wayland | ? NEEDS HUMAN | `REQUIREMENTS.md:5,19` maps this requirement to Phase 3, but the plan frontmatter does not declare a `requirements` field. Code evidence supports overlay/frame-sync/export behavior; final Wayland correctness still needs manual validation. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| --- | --- | --- | --- | --- |
| `native/qt6/src/preview_overlay.cpp` | 4640, 4673 | `QThread::msleep(100)` on export restore path | ⚠️ Warning | Blocks the UI thread during batch export/restore; does not invalidate the phase goal, but is not ideal. |
| `native/qt6/src/preview_overlay.cpp` | 563 | Tooltip advertises `(A)` toggle shortcut, but no `Qt::Key_A` handler or `QShortcut` was found in `PreviewOverlay` | ⚠️ Warning | Shortcut preservation is not proven by code, despite the tooltip claim. |
| `03-01-PLAN.md` | 1-11 | No `requirements:` frontmatter despite Phase 3 being mapped to `FEDORA43-ANNOTATION` | ℹ️ Info | Requirement traceability is incomplete; accounted for here as an orphaned requirement. |

### Human Verification Required

### 1. Wayland live video annotation overlay

**Test:** On Fedora 43 Wayland, open a video in the preview overlay, enable annotation mode, and draw/move annotations while the tlRender viewport remains visible.
**Expected:** The live video surface stays visible behind the transparent overlay; no black frame, widget swap, or lost focus prevents editing.
**Why human:** Requires real Wayland compositor behavior and interactive rendering.

### 2. Frame-accurate scrub/step/export on video and sequence media

**Test:** Annotate multiple frames in both a video and an image sequence, then scrub, use `,` / `.` stepping, and export individual/all annotated frames.
**Expected:** Each frame reloads the correct annotations, timeline markers reflect annotated frames, and exports match the visible frame plus moved annotations.
**Why human:** Needs real playback/media assets and visual comparison of exported output.

### Gaps Summary

No code-level blocker was found against the derived must-haves. The remaining unverified surface is the actual Fedora 43 Wayland runtime behavior, which needs human confirmation.

### Post-Verification Regression And Recovery

After the initial verification, Fedora 43 Wayland playback regressed during follow-up execution work. The regression was traced to the Wayland platform policy being set back to prefer the native tlRender `QOpenGLWidget` path:

- `PlatformSession::shouldUseDesktopOpenGLOnWayland()` returned `true`
- `PlatformSession::shouldUseRasterPreviewFallbackOnWayland()` returned `false`

That reintroduced the failing Wayland context path for both embedded previews and overlay playback, producing repeated `QOpenGLWidget: Failed to make context current` / `QWaylandGLContext::makeCurrent` errors. Playback was restored by returning Wayland to the raster preview fallback path and keeping tlRender initialization on `DefaultSurfaceFormat::None` for Wayland sessions. The project-manager preview path was also guarded so hidden side-panel previews do not instantiate a video widget unnecessarily.

---

_Verified: 2026-04-11T09:16:55Z_
_Verifier: OpenCode (gsd-verifier)_
