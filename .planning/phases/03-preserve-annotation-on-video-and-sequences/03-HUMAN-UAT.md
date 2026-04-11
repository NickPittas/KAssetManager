---
status: in_progress
phase: 03-preserve-annotation-on-video-and-sequences
source: [03-VERIFICATION.md]
started: 2026-04-11T09:16:55Z
updated: 2026-04-11T12:50:00Z
---

## Current Test

Wayland playback regression recovered; annotation-specific UAT still pending.

## Tests

### 1. Wayland live video annotation overlay
expected: On Fedora 43 Wayland, entering annotation mode keeps the live tlRender video visible and draws/editing occurs on the transparent overlay without blacking out or replacing the video surface.
result: pending
notes: Playback had temporarily regressed because Wayland was routed back to the native `QOpenGLWidget` tlRender viewport path. Runtime playback now works again after restoring the raster preview fallback policy.

### 2. Frame-accurate video/sequence annotation scrub and export
expected: Annotations stay attached to the correct video/sequence frames while stepping and scrubbing, and exported annotated frames match the visible frame plus moved annotations.
result: pending

## Summary

total: 2
passed: 0
issues: 1
pending: 2
skipped: 0
blocked: 0

## Gaps

- Resolved runtime regression: Wayland playback failed with repeated `QOpenGLWidget: Failed to make context current` errors after the platform policy was switched back to desktop OpenGL/native tlRender viewport mode. Restored by routing Wayland previews back through the raster fallback path.
