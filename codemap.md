# linux-port-fedora43-wt/

<!-- Explorer: Fill in this section with architectural understanding -->

## Responsibility

<!-- What is this folder's job in the system? -->

## Design

<!-- Key patterns, abstractions, architectural decisions -->

## Flow

<!-- How does data/control flow through this module? -->

## Integration

<!-- How does it connect to other parts of the system? -->
# Repository Atlas: KAssetManager (linux-port-fedora43-wt)

## Project Responsibility
Qt 6 / C++20 desktop asset manager with three major shells inside one application:
- Asset Manager for the indexed library
- File Manager for direct filesystem browsing
- Project Manager for watch-folder based project ingest

This worktree contains branch-specific Linux/Wayland preview behavior and must be treated as the source of truth for current playback and annotation work.

## Entry Points
- `native/qt6/src/main.cpp`: process startup, Qt application attributes, Wayland/OpenGL toggles, database initialization, main window creation
- `native/qt6/src/mainwindow.cpp`: top-level UI orchestration for Asset Manager, File Manager, Project Manager, overlay launching, and preview routing
- `native/qt6/CMakeLists.txt`: authoritative build wiring for app, tlRender integration, Qt modules, and platform dependency discovery

## Branch-Specific Verified Findings
- Wayland raster preview fallback is enabled unconditionally by `PlatformSession::shouldUseRasterPreviewFallbackOnWayland()` returning `isWayland()`.
- `PreviewOverlay` video annotation in this worktree uses a transparent `annotationOverlayView` over the active video widget, not the still-image `imageScene` path described in the older checkout.
- `TLRenderViewport` has two rendering paths:
  - native `tl::qtwidget::Viewport`
  - Wayland raster fallback via `QLabel` fed by `TLRenderPlayer::getCurrentFrame()`

## Directory Map
| Directory | Responsibility | Detailed Map |
|-----------|----------------|--------------|
| `native/qt6/` | Qt application build, dependency wiring, and test target definitions. | `native/qt6/codemap.md` |
| `native/qt6/src/` | Main application code: window shell, models, preview overlays, importers, DB access, file operations. | `native/qt6/src/codemap.md` |
| `native/qt6/src/media/` | tlRender player, viewport wrappers, OpenGL widget, and preview rendering integration. | `native/qt6/src/media/codemap.md` |
| `scripts/` | Windows build and packaging automation. | `scripts/codemap.md` |

## Preview / Annotation Flow
1. `MainWindow` selects an asset and opens `PreviewOverlay`.
2. `PreviewOverlay::showVideo()` ensures `TLRenderPlayer` and `TLRenderViewport` exist, loads media, and starts playback.
3. On Wayland, `TLRenderViewport` uses raster fallback and paints a `QLabel` pixmap from `TLRenderPlayer::getCurrentFrame()`.
4. When annotation mode is enabled for video, `PreviewOverlay` keeps the live video widget visible and layers `annotationOverlayView` above it.
5. Annotation input is converted from overlay view coordinates to scene coordinates in `PreviewOverlay::eventFilter()` and forwarded into `AnnotationLayer`.

## Verification Scope
This atlas was rebuilt from the active worktree under:
`/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt`

Older codemap files from the main checkout should be treated as background context only.
