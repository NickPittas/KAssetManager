# native/qt6/src/

<!-- Explorer: Fill in this section with architectural understanding -->

## Responsibility

<!-- What is this folder's job in the system? -->

## Design

<!-- Key patterns, abstractions, architectural decisions -->

## Flow

<!-- How does data/control flow through this module? -->

## Integration

<!-- How does it connect to other parts of the system? -->
# native/qt6/src/

## Responsibility
Implements the main Qt Widgets application, including data models, project/file management, preview overlays, annotation tools, import workflows, and persistence.

## Main Shells
- `main.cpp`: startup, Wayland session detection, Qt attributes, persistent DB initialization
- `mainwindow.cpp`: primary orchestration hub for Asset Manager, File Manager, and Project Manager
- `preview_overlay.cpp`: full-screen preview shell for image, sequence, video, PDF, text, and annotation workflows
- `image_preview_overlay.cpp`: lighter image-only preview variant

## Key Design Patterns
- Centralized UI orchestration in `MainWindow` and `PreviewOverlay`
- Model/view usage for library, filesystem, and project panes
- Background decode/import work via QtConcurrent and thread pools
- Scene-based annotation editing through `AnnotationLayer` + `AnnotationItem` hierarchy

## Preview and Annotation Flow
1. `MainWindow` or File Manager opens `PreviewOverlay` for the selected asset.
2. `PreviewOverlay` routes by media type:
   - images/sequences through `QGraphicsScene`/`QGraphicsView`
   - video through `TLRenderPlayer` + `TLRenderViewport`
3. In this branch, video annotation mode keeps the video widget visible and overlays `annotationOverlayView` on top of it.
4. `PreviewOverlay::eventFilter()` translates mouse events from the active annotation surface into scene coordinates and forwards them to `AnnotationLayer`.
5. `AnnotationLayer` creates concrete `AnnotationItem` objects and serializes them per frame in `frameAnnotations`.

## Verified Active Files For Current Preview Work
- `preview_overlay.cpp/.h`
- `annotation_layer.cpp/.h`
- `annotation_items.cpp/.h`
- `icon_utils.cpp/.h`
- `platform_session.h`

## Verified Branch-Specific Findings
- `PlatformSession::shouldUseRasterPreviewFallbackOnWayland()` always returns `isWayland()`, so Wayland preview defaults to the raster path.
- `PreviewOverlay::enableAnnotationMode()` for video sets `annotationLayer` to `annotationOverlayScene`, not `imageScene`.
- `PreviewOverlay::captureCurrentFrame()` still depends on `TLRenderPlayer::getCurrentFrame()` for video export and annotation capture.

## Integration
- Consumed by: `kassetmanagerqt`
- Depends on: media layer in `src/media/`, DB layer, theme/log/progress managers, tlRender feature flags
