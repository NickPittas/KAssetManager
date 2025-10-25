# Qt 6 Refactor Plan (Option 1)

Author: Augment Agent
Date: 2025-10-24
Scope: Replace Electron front-end with a Windows‑native Qt 6 app that supports external drag‑and‑drop and professional media viewing.

---

## Objectives
- Enable robust drag & drop between app and external applications (Explorer, Photoshop, Nuke, AE, Premiere, Resolve, Fusion, etc.).
- Support professional media: EXR/DPX/TIFF/PSD/IFF/PNG/JPG; video playback for MOV/MP4/MKV/ProRes/H.264/H.265, etc.
- Preserve and improve Explorer‑like UX (folder tree, grid, filters/tags/ratings, info panel) and integrate with existing backend where practical.
- Avoid Python deps and fragile wheels; ship stable, prebuilt Windows libraries.

## Chosen Stack
- UI: Qt 6.7+ (Qt Quick/QML + C++ backend).
- Video: Qt Multimedia (FFmpeg backend) with D3D11/DXVA acceleration; fallback to embedded libmpv if needed for edge cases.
- Images: OpenImageIO (C++) for EXR, DPX, TIFF, PSD, IFF, etc. (no Python bindings).
- DnD: Qt QDrag/QMimeData for CF_HDROP; custom Windows OLE IDataObject for FILEDESCRIPTORW/FILECONTENTS (virtual files / delayed rendering).
- Build/Ship: CMake + MSVC; vcpkg for OIIO/OpenEXR stack; windeployqt + bundling DLLs.

## External Drag & Drop (Windows)
- Real files: Use CF_HDROP via QMimeData URLs (text/uri-list). Broadly accepted by Explorer and most creative apps.
- Virtual files (not on disk): Implement custom IDataObject exposing:
  - CFSTR_FILEDESCRIPTORW (one or many FILEDESCRIPTORW via FILEGROUPDESCRIPTORW)
  - CFSTR_FILECONTENTS (provide IStream per file through TYMED_ISTREAM, delayed rendering)
- Strategy:
  1) Attempt virtual FILECONTENTS first for zero-copy when target supports it.
  2) If target requests CF_HDROP only: stream asset to a temp staging path, then advertise CF_HDROP.
  3) Incoming drops: accept CF_HDROP/text/uri-list for import; future: app-specific formats if needed.

## Media Pipeline
- Video playback: Qt Multimedia player + VideoOutput; timecode, play/pause, seek, volume, keyboard shortcuts; accurate seeking best-effort with FFmpeg.
- Image viewing: OIIO for decode + metadata; generate thumbnails (background) and mips; GPU upload; optional OpenColorIO later (ACES/sRGB).
- Thumbnail generation: Prefer in-process decode (OIIO/libav) over shelling out, to simplify env/flags and reduce errors.

## UI Architecture (QML + C++)
- Views: Explorer‑style TreeView (originalFolder hierarchy), GridView/ListView for assets, Filters panel (tags/categories/rating/file types), Info panel, Player/Viewer panel.
- Models: QAbstractItemModel for tree and assets; QSortFilterProxy for filtering; async loaders for thumbnails/metadata.
- Interaction: Keyboard navigation (arrow keys), proper drag behavior without text selection, context menus.
- Theming: Neutral dark palette (“shadcn vibe”), design tokens for colors/spacing/typography.

## Migration Milestones
- M0: Spike (3–5 days)
  - Qt app skeleton (QML + C++)
  - External drag: CF_HDROP drag‑out, drag‑in (Explorer → app)
  - Virtual file drag: FILEDESCRIPTORW/FILECONTENTS to Explorer
  - Basic video player + EXR/DPX/PSD viewer
  - Verify against Photoshop/Nuke/AE/Resolve/Fusion/Explorer
- M1: Core UI parity
  - Explorer-like tree + virtualized asset grid + info panel + filters + theming
  - Import button + ingest pipeline + “Show in Explorer”
- M2: Data/services
  - Backup/restore UI integration + CSV import/export + tag/rating features + AI assistant hookup
- M3: Perf + packaging
  - Caching, responsiveness for large images, error handling/logging; packaging (windeployqt), licensing files, verification matrix, profiling

## Mapping to Current Tasks
- Folder hierarchy view / Explorer-style tree → QML TreeView + model
- Theming/colors → QML theme tokens
- Import Assets button → QML Button + QFileDialog + ingestion
- Drag FROM Explorer TO app → accept QDropEvent.urls
- Show in Explorer → QDesktopServices::openUrl(file://…)
- Backup endpoint 500 → Frontend surfaces errors; fix backend handler
- FFmpeg thumbnail generation → Replace with in-process decode; or minimal ffmpeg CLI with safe args where unavoidable
- Video player with controls → Qt Multimedia player UI

## Dependencies & Packaging
- Use vcpkg manifest for: openimageio, openexr, libtiff, libjpeg‑turbo, zlib.
- Bundle Qt Multimedia FFmpeg plugins and OIIO/OpenEXR runtime DLLs.
- Use windeployqt for Qt; add license attributions for FFmpeg/OIIO.

## Verification Matrix (initial)
- Explorer: drag‑out/in real and virtual files
- Photoshop: drag image assets (PNG/JPG/TIFF/PSD) in
- Nuke: drag media script/read nodes where supported; else CF_HDROP
- After Effects / Premiere: drag video; confirm accept
- Resolve / Fusion: drag media to media pool/timeline

## Risks & Mitigations
- DnD virtual files rejected by some apps → temp CF_HDROP fallback
- Codec edge cases in Qt Multimedia → fallback to embedded libmpv for those files
- OIIO size/perf on huge EXR/DPX → use tiles/strips, background loading, caching, mip maps
- Packaging pitfalls (DLL hell) → deterministic vcpkg + CI packaging script + smoke tests

## Next Steps (Execution)
1) Create native/qt6 project skeleton (CMake, QML, main, minimal CF_HDROP helper)
2) Implement CF_HDROP drag‑out and drag‑in
3) Implement virtual files via IDataObject (FILEDESCRIPTORW/FILECONTENTS)
4) Add player/viewer basics (Qt Multimedia, OIIO)
5) Build out M1 UI parity and remaining tasklist items

---

This document intentionally stays implementation‑focused to drive continuous progress. See tasklist for detailed actionable items and status.

