# Live Scrubbing Migration Plan

## Goals
- Replace disk-based thumbnail generation with live preview and scrubbing in asset manager and file manager grid views.
- Preserve asset database registration and metadata flows.
- Deliver responsive, high-quality playback previews suitable for VFX workflows.
- Keep previews in memory only; avoid regenerating or storing disk thumbnails.

## Current State
- `LivePreviewManager` now powers both Asset Manager and File Manager grids. Frames stream in-memory, cards render a consistent inset preview, and scrubbing maps directly across the card width with cursor clamping.
- The application ships with a custom FFmpeg full-shared build (via `scripts/fetch-ffmpeg.ps1`) covering intraframe codecs such as PNG-in-MOV, ProRes, DNxHD, MXF, DNxHR, and Animation. `scripts/build-windows.ps1` stages the matching DLLs into the portable bundle.
- Hover scrubbing standardises on `Ctrl` + mouse movement (or wheel) with a slim HUD overlay and shared delegates in both grids. Cards include a neutral background so previews stay visually bounded.
- UI copy, build scripts, and docs refer to **live previews** instead of disk thumbnails, and the File Manager toolbar exposes a "Group sequences" checkbox beside the preview toggle.

## Architecture Plan
1. **FFmpeg-First Decode Pipeline**
   - Produce and ship a dedicated FFmpeg build with the full codec set (PNG, ProRes, DNxHD, MXF OP1a/Atom, animation, etc.) and required dependencies (libpng, zlib, bz2, openjpeg).
   - Update CMake/tooling to link against the custom build and copy the matching DLLs into `dist/portable/bin`.
   - Remove Qt Multimedia/legacy thumbnail fallbacks once decode coverage is confirmed.
   - Maintain a small worker pool with cooperative cancellation for off-screen items.

2. **Smart In-Memory Caching**
   - Use an LRU cache keyed by `(filePath, positionBucket, targetSize)` storing `QPixmap`/`QImage`.
   - Support configurable caps (default ~512 MB) and expose manual/automatic eviction hooks.
   - Reuse decoder state while a user scrubs the same clip to minimise seeks.

3. **Image Sequence Handling**
   - Treat EXR/DPX image sequences as a dedicated pipeline using OpenImageIO tiled reads.
   - Stream frames incrementally with watchdog timeouts and throttle concurrency to avoid UI stalls.
   - Cache per-sequence metadata (frame list, fps, dynamic range) for quick lookups.

4. **Asset Grid Refinement**
   - Continue sourcing frames via `PreviewStateRole`, but render the decoded frame directly inside the card.
   - Standardise on `Ctrl + wheel` for scrubbing; optional horizontal mouse motion can remain as a secondary gesture.
   - Replace the large scrollbar overlay with a slim HUD strip that indicates playhead position, frame number, and errors.
   - Request poster frames as cells enter the viewport and cancel requests on exit; report decode failures inline.

5. **File Manager Grid Update**
   - Share the same live-preview delegate/overlay as the asset grid.
   - Adjust layout metrics so filenames never overlap the overlay and remain readable in all DPI modes.
   - Ensure grouped sequences expose scrub metadata and respect the same gesture mapping.

6. **Shared Preview Overlay**
   - Centralise hover detection, modifier tracking, wheel-to-timeline mapping, and debounce logic inside a `GridPreviewController`.
   - Clamp overlay geometry to card bounds so frames never render off-screen or below the thumbnail area.
   - Integrate with `LivePreviewManager` to cancel outstanding requests when focus leaves the cell.

7. **Settings & UI Cleanup**
   - Remove obsolete thumbnail cache actions from menus, toolbars, and the status bar.
   - Add preferences controlling live scrubbing enablement, cache size, and sequence safety thresholds.
   - Update labels/tooltips to reference “live previews” instead of “thumbnails”.

8. **Tooling & Documentation**
   - Drop `thumbnail_generator.*` from CMake once no longer referenced and exclude `data/thumbnails/` from packages.
   - Document the FFmpeg build recipe and live preview workflow in `DEVELOPER_GUIDE.md`, `USER_GUIDE.md`, and `API_REFERENCE.md`.

9. **Testing Matrix**
   - Manual validation per `DEVELOPER_GUIDE`: still images, H.264/H.265, ProRes, DNxHD, MXF, PNG-in-MOV, and heavy EXR/DPX sequences.
   - Stress-test large directories while scrubbing to verify cache eviction, worker cancellation, and UI responsiveness.
   - Confirm asset/file manager parity, keyboard navigation, drag-and-drop, overlay visibility, and modifier behaviour.

## Follow-Ups
- Explore hardware decode paths (NVDEC/VAAPI/D3D11VA) after the FFmpeg-first pipeline is stable.
- Investigate OCIO integration for HDR scrubbing once baseline performance targets are met.
