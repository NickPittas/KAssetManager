# Live Scrubbing Migration Plan

## Goals
- Replace disk-based thumbnail generation with live preview and scrubbing in asset manager and file manager grid views.
- Preserve asset database registration and metadata flows.
- Deliver responsive, high-quality playback previews suitable for VFX workflows.
- Keep previews in memory only; avoid regenerating or storing disk thumbnails.

## Current State
- `LivePreviewManager` has replaced the legacy thumbnail generator for both grids, but decode failures keep the cards blank and the overlay shows noise instead of real frames.
- The bundled FFmpeg build (BtbN `ffmpeg-master-latest-win64-gpl-shared`) lacks critical intraframe decoders such as PNG-in-MOV, ProRes, DNxHD, and several MXF variants. Any clip using those codecs logs `Decoder not found` and never returns a frame.
- Hover scrubbing is wired to `Ctrl + wheel`, yet the overlay still renders as a tall scrollbar anchored to the card edge, and filenames in the file manager grid are clipped because the label sits too close to the bottom margin.
- Settings expose a live preview cache flush, but UI text and docs still refer to “thumbnails”.

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
