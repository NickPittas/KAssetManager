# KAsset Manager Qt - Task List

## Completed Tasks (DONE)
- Stabilize asset list refresh: Clamp list column widths and harden highlight logic so large folder imports no longer crash the QML list delegate.
- Make log entries selectable: Render the activity log rows with read-only TextEdit so operators can copy/paste entries.
- Replace deprecated Qt.htmlEscape usage: Use Qt.htmlEscaped/manual escaping in highlightMatch to keep imports from crashing on new assets in Qt 6.
- Gate Qt debug logging: Ensure verbose Qt logging (qt.qml/qt.quick/etc.) only runs in diagnostics mode to prevent startup freezes from log storms.
- Restore QML singleton registration: Register LogManager/ProgressManager/ThumbnailGenerator and asset models with the QML engine after refactor so logging/import continue working.
- Populate activity log tab: Ensure LogManager entries appear in UI and key operations emit log messages.
- Enhance asset list/grid metadata: Improve readability of asset views with name, type, size, modified, and tags placeholders using dark theme colors.
- Preview interactions polish: Enable double-click/space shortcuts and keyboard arrows to open and navigate the preview overlay.
- Compact dark header styling: Reduce top-bar whitespace and align theme styling with dark palette requirements.
- Add Import Assets button to UI: Add a prominent button to import files/folders into the app.
- Implement drag and drop FROM Windows Explorer TO app: Allow users to drag files/folders from Windows Explorer into the app to import them.
- Add 'Show in Explorer' context menu option: Right-click menu option to open the asset's location in Windows Explorer.
- Add video player with controls: Implement video player with play/pause, timeline scrubbing, and standard video controls for all video file types (mov, mp4, mkv, etc.). Currently no video preview capability exists.
- Folder CRUD + asset move UI: Add context menu on the virtual folder tree (new/rename/delete with virtual-only confirmation) and enable drag-to-reparent folders; support dragging assets from the grid onto folders to move them virtually.
- Preview shortcuts: Support double-click and spacebar to open the asset preview/lightbox with keyboard navigation for next/previous and close.
- Qt 6 Refactor (Option 1) — Master Epic: Deliver a Windows-native Qt 6 front end with external drag-and-drop, pro media viewing, and UI parity. See QT6-REFACTOR-PLAN.md for scope, milestones, risks, verification matrix.
  - M0: Virtual files DnD via IDataObject (FILEDESCRIPTORW/FILECONTENTS): Implement custom IDataObject for delayed rendering; support multi-file virtual drags; temp CF_HDROP fallback.
  - M0: Basic video player (Qt Multimedia): Add player with play/pause/scrub/volume/timecode; keyboard shortcuts; verify common codecs.
- Asset type filter (All/Images/Videos): Added to AssetsModel with header buttons in Main.qml; filtering integrated with ThumbnailGenerator helpers.
- Tags panel under Folders: With "+" dialog to name new tags; drag selected assets onto a tag to assign; DB schema tables (tags, asset_tags) and AssetsModel.assignTags wired.
- Right-click behavior fixed: Per-asset context menu on tiles; bulk selection menu when right-clicking empty space in grid/list; no overlay blocks the search/filters.
- Grid tiles resized and clipped: So metadata no longer overlaps; show name/type/size/modified and Tags.
- Info panel shows Tags for selected asset: Preview components stabilized.
- QSettings identifiers set in main.cpp: So Settings persist.
- Build/packaging script verified: Portable build output at dist/portable.

## Pending Tasks (PENDING)
- Update interface colors to shadcn style: Change color scheme to more black and white shadcn vibe instead of current colors.
- Fix backup endpoint 500 error: Backend endpoint /backup is returning 500 Internal Server Error.
- Fix FFmpeg thumbnail generation commands: Research and implement correct FFmpeg commands for each video file type (mov, mp4, mkv, etc.). Current commands use excessive/incorrect arguments causing format conversion errors. Need simple, working commands for thumbnail generation.
- Qt 6 Refactor (Option 1) — Master Epic (continued):
  - M0: OIIO image viewer (EXR/DPX/PSD/PNG/JPG): Integrate OpenImageIO for decode + metadata; show large images efficiently; generate thumbnails.
  - M0: Verification matrix run & notes: Test drag-out/in with Explorer/Photoshop/Nuke/AE/Resolve/Fusion; document results and compatibility quirks.
  - M1: Asset grid + virtualization + keyboard nav: GridView/ListView with thumbnail cache; arrow navigation and selection; drag handles.
  - M1: Filters panel (tags/categories/rating/type): Visible filter panels with checklists and rating stars; proxy model filters.
  - M1: Info/metadata panel: Display technical metadata; OIIO for images, libavformat for video; editable fields where applicable.
  - M1: Theming to shadcn vibe: Apply neutral dark theme; tokens for colors/spacing/typography; consistent components.
  - M1: Import Assets button + ingestion: Button opens QFileDialog for files/folders; route to ingestion pipeline; progress UI.
  - M1: Show in Explorer: Context action opens file location via QDesktopServices/ShellExecuteW.
  - M2: Backup/restore UI + fix /backup 500: Frontend progress + error surfacing; investigate and fix backend 500; add retries and logging.
  - M2: CSV import/export module: RFC4180-compliant CSV reader/writer; map to asset schema; UI hooks.
  - M2: Tags & ratings integration: UI for applying tags/ratings; model updates; persistence; filters interop.
  - M2: AI assistant integration: Connect to existing AI endpoint; async suggestions/metadata generation; UX affordances.
  - M3: Thumbnail pipeline (replace brittle ffmpeg CLI): Switch to in-process OIIO/libav thumbnailers; or standardize minimal ffmpeg CLI when necessary; consistent outputs.
  - M3: Performance & memory for huge images: Tiled/striped IO, mip levels, background loading, cache bounds; avoid UI jank.
  - M3: Packaging & licenses: windeployqt; bundle OIIO/OpenEXR DLLs; add license attributions; smoke-test packaged app.
  - M3: Logging, error handling, profiling: Structured logging; user-friendly errors; perf profiling; write verification notes.
- Clean up old backend/frontend/electron: Remove backend/, frontend/, electron/, node scripts and configs that are no longer used by the Qt6 app after confirming no references remain; keep dist/portable and native/qt6.
- Implement thumbnail generation for videos: Add video thumbnail extraction using Qt Multimedia or FFmpeg to generate preview frames from video files. Store in data/thumbnails/ directory.
- Add filters panel UI: Implement collapsible filters panel with: file type filters (images/videos/audio/documents), tag filters, rating filters, date filters. Should be visible and functional like Windows Explorer filters.
- Implement tags system: Add tags table to database, tag assignment UI, tag filtering, and tag management (create/rename/delete tags).
- Implement rating system: Add rating column to assets table (0-5 stars), rating UI in info panel and grid overlay, rating filter in filters panel.
- Test and fix all CRUD operations: Test folder create/rename/delete operations. Test asset move operations. Verify database updates correctly and UI refreshes properly.
- Implement collections system: Allow users to create, rename, and delete collections (virtual groupings) and add/remove assets; expose filtering by collection across UI.
- Settings panel enhancements: Convert the current Settings tab into user-facing preferences (theme, cache size, default view, shortcuts) stored persistently.
- Performance Optimization - Thumbnail Generation: LOW PRIORITY: Once functionality works, optimize: 1) Adjust thread pool size, 2) Implement thumbnail priority queue, 3) Add thumbnail generation cancellation.
- Error Handling - Failed Thumbnail Generation: LOW PRIORITY: Add proper error handling: 1) Show error icon for failed thumbnails, 2) Log thumbnail generation errors, 3) Retry mechanism for transient failures.
- Add asset preview panel: Enhance info panel to show full-size image preview or video player for selected asset. Add metadata display (dimensions, duration, codec, etc).
- Implement asset search bar: Add a search field that filters assets by filename, tags, and metadata in real time; integrate with AssetsModel filtering and highlight matches.
- Implement collections system: Allow users to create, rename, delete collections (virtual groupings) and add/remove assets; expose in UI alongside folders and enable filtering by collection.
- Settings panel enhancements: Convert debug Settings tab into user-facing preferences (theme, cache size, default view, shortcuts) persisted across sessions.

## Latest updates (2025-10-25)
- DONE: Asset type filter (All/Images/Videos) added to AssetsModel with header buttons in Main.qml; filtering integrated with ThumbnailGenerator helpers.
- DONE: Tags panel under Folders with "+" dialog to name new tags; drag selected assets onto a tag to assign; DB schema tables (tags, asset_tags) and AssetsModel.assignTags wired.
- DONE: Right-click behavior fixed: per-asset context menu on tiles; bulk selection menu when right-clicking empty space in grid/list; no overlay blocks the search/filters.
- DONE: Grid tiles resized and clipped so metadata no longer overlaps; show name/type/size/modified and Tags.
- DONE: Info panel shows Tags for the selected asset; preview components stabilized.
- DONE: QSettings identifiers set in main.cpp so Settings persist.
- DONE: Build/packaging script verified; portable build output at dist/portable.

## Next actions
- Tag filtering in model and UI (multi-select, intersection/union modes).
- Ratings: add DB column migration + UI stars in grid/info + batch set + filtering.
- Bulk menu: add "Assign Tag…" submenu and quick rating actions parity in list view.
- Tests: add smoke tests for tag CRUD and assignment; add diagnostics for filter changes.
- Docs: add user guide section for Filters/Tags and update screenshots.
