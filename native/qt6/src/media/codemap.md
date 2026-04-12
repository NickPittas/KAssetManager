# native/qt6/src/media/

<!-- Explorer: Fill in this section with architectural understanding -->

## Responsibility

<!-- What is this folder's job in the system? -->

## Design

<!-- Key patterns, abstractions, architectural decisions -->

## Flow

<!-- How does data/control flow through this module? -->

## Integration

<!-- How does it connect to other parts of the system? -->
# native/qt6/src/media/

## Responsibility
Provides the media playback backend and preview rendering bridge between Qt Widgets and tlRender.

## Components
- `tlrender_player.cpp/.h`: media loading, playback control, tlRender context ownership, cached frame access, OCIO/display options
- `tlrender_viewport.cpp/.h`: QWidget wrapper that chooses between native `tl::qtwidget::Viewport` and Wayland raster fallback
- `tlrender_widget.cpp/.h`: older direct `QOpenGLWidget` renderer that draws `currentVideoFrames()` manually; present in the branch but not the active `PreviewOverlay` video path

## Active Playback Paths
### Native viewport path
1. `TLRenderViewport::ensureViewport()` creates `tl::qtwidget::Viewport`.
2. `setPlayer()` binds the shared `tl::qt::PlayerObject` from `TLRenderPlayer`.
3. tlRender paints frames internally.

### Wayland raster fallback path
1. `PlatformSession::shouldUseRasterPreviewFallbackOnWayland()` returns true on Wayland.
2. `TLRenderViewport` creates `m_rasterLabel` instead of native viewport.
3. `updateRasterFrame()` calls `m_player->getCurrentFrame(targetSize)`.
4. The returned `QImage` is converted to a `QPixmap` and displayed centered inside the label.

## Verified Branch-Specific Findings
- The active Fedora worktree uses the raster fallback on Wayland by policy, not as a rare fallback.
- `displayedContentRect()` computes overlay geometry from the raster label's fitted pixmap size when the raster path is active.
- `PreviewOverlay` relies on that rect through `syncAnnotationOverlayToVideo()` to place the annotation overlay.
- `TLRenderPlayer::getCurrentFrame()` currently prefers cached tlRender frames and falls back to FFmpeg thumbnail extraction.

## Current Risk Areas
- Any bug in `getCurrentFrame()` affects both Wayland raster playback appearance and video annotation/export capture.
- Any mismatch between `displayedContentRect()`, overlay view geometry, and annotation scene rect will break visible drawing even when annotation items exist.

## Integration
- Consumed by: `PreviewOverlay`, File Manager preview surfaces, other video preview entry points
- Depends on: tlRender timeline/player/Qt wrappers, feather-tk image types, platform session policy
