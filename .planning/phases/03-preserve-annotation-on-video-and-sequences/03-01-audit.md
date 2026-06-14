## Task 1 Audit Findings

- Existing work already fixed annotation JSON serialization for moved text, freehand, rectangle, ellipse, and arrow items in `annotation_items.cpp`, with focused coverage in `test_annotation_items.cpp`.
- Existing work also deferred tlRender player/viewport creation in `preview_overlay.cpp`, which removed the eager Wayland GL surface creation that was destabilizing preview startup.
- The remaining blocking gap for this plan is in video annotation mode: `PreviewOverlay::enableAnnotationMode()` still switches video annotation into `imageView` instead of keeping a transparent annotation layer above the live tlRender viewport.
- A smaller correctness gap remains in sequence preview state: `showSequence()` does not reset annotation session state or refresh `currentFilePath/currentFileType`, which can leak markers between sequences and produce incorrect export naming.

## Planned Minimal Fix Scope

1. Keep video annotation rendering on a transparent overlay view positioned above the tlRender viewport.
2. Preserve frame-accurate save/load when scrubbing or stepping video frames without replacing the live playback surface.
3. Reset sequence annotation session state at sequence open and refresh sequence export metadata.
