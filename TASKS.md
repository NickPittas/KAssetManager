-[/] NAME:Stabilize asset list refresh DESCRIPTION:Clamp list column widths and harden highlight logic so large folder imports no longer crash the QML list delegate.
-[/] NAME:Make log entries selectable DESCRIPTION:Render the activity log rows with read-only TextEdit so operators can copy/paste entries.
-[/] NAME:Stabilize asset list refresh DESCRIPTION:Clamp list column widths and harden highlight logic so large folder imports no longer crash the QML list delegate.
-[/] NAME:Make log entries selectable DESCRIPTION:Render the activity log rows with read-only TextEdit so operators can copy/paste entries.
-[/] NAME:Replace deprecated Qt.htmlEscape usage DESCRIPTION:Use Qt.htmlEscaped/manual escaping in highlightMatch to keep imports from crashing on new assets in Qt 6.
-[/] NAME:Gate Qt debug logging DESCRIPTION:Ensure verbose Qt logging (qt.qml/qt.quick/etc.) only runs in diagnostics mode to prevent startup freezes from log storms.
-[/] NAME:Restore QML singleton registration DESCRIPTION:Register LogManager/ProgressManager/ThumbnailGenerator and asset models with the QML engine after refactor so logging/import continue working.
-[/] NAME:Populate activity log tab DESCRIPTION:Ensure LogManager entries appear in UI and key operations emit log messages.
-[/] NAME:Enhance asset list/grid metadata DESCRIPTION:Improve readability of asset views with name, type, size, modified, and tags placeholders using dark theme colors.
-[/] NAME:Preview interactions polish DESCRIPTION:Enable double-click/space shortcuts and keyboard arrows to open and navigate the preview overlay.
-[/] NAME:Compact dark header styling DESCRIPTION:Reduce top-bar whitespace and align theme styling with dark palette requirements.
-[ ] NAME:Update interface colors to shadcn style DESCRIPTION:Change color scheme to more black and white shadcn vibe instead of current colors.
-[/] NAME:Add Import Assets button to UI DESCRIPTION:Add a prominent button to import files/folders into the app
-[/] NAME:Implement drag and drop FROM Windows Explorer TO app DESCRIPTION:Allow users to drag files/folders from Windows Explorer into the app to import them
-[/] NAME:Add 'Show in Explorer' context menu option DESCRIPTION:Right-click menu option to open the asset's location in Windows Explorer
-[ ] NAME:Fix backup endpoint 500 error DESCRIPTION:Backend endpoint /backup is returning 500 Internal Server Error
-[ ] NAME:Fix FFmpeg thumbnail generation commands DESCRIPTION:Research and implement correct FFmpeg commands for each video file type (mov, mp4, mkv, etc.). Current commands use excessive/incorrect arguments causing format conversion errors. Need simple, working commands for thumbnail generation.
-[/] NAME:Add video player with controls DESCRIPTION:Implement video player with play/pause, timeline scrubbing, and standard video controls for all video file types (mov, mp4, mkv, etc.). Currently no video preview capability exists.
-[ ] NAME:Qt 6 Refactor (Option 1) â€” Master Epic DESCRIPTION:Deliver a Windows-native Qt 6 front end with external drag-and-drop, pro media viewing, and UI parity. See QT6-REFACTOR-PLAN.md for scope, milestones, risks, verification matrix.
--[/] NAME:M0: Virtual files DnD via IDataObject (FILEDESCRIPTORW/FILECONTENTS) DESCRIPTION:Implement custom IDataObject for delayed rendering; support multi-file virtual drags; temp CF_HDROP fallback.
--[/] NAME:M0: Basic video player (Qt Multimedia) DESCRIPTION:Add player with play/pause/scrub/volume/timecode; keyboard shortcuts; verify common codecs.
--[ ] NAME:M0: OIIO image viewer (EXR/DPX/PSD/PNG/JPG) DESCRIPTION:Integrate OpenImageIO for decode + metadata; show large images efficiently; generate thumbnails.
--[ ] NAME:M0: Verification matrix run & notes DESCRIPTION:Test drag-out/in with Explorer/Photoshop/Nuke/AE/Resolve/Fusion; document results and compatibility quirks.
--[ ] NAME:M1: Asset grid + virtualization + keyboard nav DESCRIPTION:GridView/ListView with thumbnail cache; arrow navigation and selection; drag handles.
--[ ] NAME:M1: Filters panel (tags/categories/rating/type) DESCRIPTION:Visible filter panels with checklists and rating stars; proxy model filters.
--[ ] NAME:M1: Info/metadata panel DESCRIPTION:Display technical metadata; OIIO for images, libavformat for video; editable fields where applicable.
--[ ] NAME:M1: Theming to shadcn vibe DESCRIPTION:Apply neutral dark theme; tokens for colors/spacing/typography; consistent components.
--[ ] NAME:M1: Import Assets button + ingestion DESCRIPTION:Button opens QFileDialog for files/folders; route to ingestion pipeline; progress UI.
--[ ] NAME:M1: Show in Explorer DESCRIPTION:Context action opens file location via QDesktopServices/ShellExecuteW.
--[ ] NAME:M2: Backup/restore UI + fix /backup 500 DESCRIPTION:Frontend progress + error surfacing; investigate and fix backend 500; add retries and logging.
--[ ] NAME:M2: CSV import/export module DESCRIPTION:RFC4180-compliant CSV reader/writer; map to asset schema; UI hooks.
--[ ] NAME:M2: Tags & ratings integration DESCRIPTION:UI for applying tags/ratings; model updates; persistence; filters interop.
--[ ] NAME:M2: AI assistant integration DESCRIPTION:Connect to existing AI endpoint; async suggestions/metadata generation; UX affordances.
--[ ] NAME:M3: Thumbnail pipeline (replace brittle ffmpeg CLI) DESCRIPTION:Switch to in-process OIIO/libav thumbnailers; or standardize minimal ffmpeg CLI when necessary; consistent outputs.
--[ ] NAME:M3: Performance & memory for huge images DESCRIPTION:Tiled/striped IO, mip levels, background loading, cache bounds; avoid UI jank.
--[ ] NAME:M3: Packaging & licenses DESCRIPTION:windeployqt; bundle OIIO/OpenEXR DLLs; add license attributions; smoke-test packaged app.
--[ ] NAME:M3: Logging, error handling, profiling DESCRIPTION:Structured logging; user-friendly errors; perf profiling; write verification notes.
-[/] NAME:Folder CRUD + asset move UI DESCRIPTION:Add context menu on the virtual folder tree (new/rename/delete with virtual-only confirmation) and enable drag-to-reparent folders; support dragging assets from the grid onto folders to move them virtually.
-[ ] NAME:Clean up old backend/frontend/electron DESCRIPTION:Remove backend/, frontend/, electron/, node scripts and configs that are no longer used by the Qt6 app after confirming no references remain; keep dist/portable and native/qt6.
-[ ] NAME:Implement thumbnail generation for videos DESCRIPTION:Add video thumbnail extraction using Qt Multimedia or FFmpeg to generate preview frames from video files. Store in data/thumbnails/ directory.
-[ ] NAME:Add filters panel UI DESCRIPTION:Implement collapsible filters panel with: file type filters (images/videos/audio/documents), tag filters, rating filters, date filters. Should be visible and functional like Windows Explorer filters.
-[ ] NAME:Implement tags system DESCRIPTION:Add tags table to database, tag assignment UI, tag filtering, and tag management (create/rename/delete tags).
-[ ] NAME:Implement rating system DESCRIPTION:Add rating column to assets table (0-5 stars), rating UI in info panel and grid overlay, rating filter in filters panel.
-[ ] NAME:Test and fix all CRUD operations DESCRIPTION:Test folder create/rename/delete operations. Test asset move operations. Verify database updates correctly and UI refreshes properly.
-[ ] NAME:Implement collections system DESCRIPTION:Allow users to create, rename, and delete collections (virtual groupings) and add/remove assets; expose filtering by collection across UI.
-[ ] NAME:Settings panel enhancements DESCRIPTION:Convert the current Settings tab into user-facing preferences (theme, cache size, default view, shortcuts) stored persistently.
-[/] NAME:Preview shortcuts DESCRIPTION:Support double-click and spacebar to open the asset preview/lightbox with keyboard navigation for next/previous and close.
-[ ] NAME:Performance Optimization - Thumbnail Generation DESCRIPTION:LOW PRIORITY: Once functionality works, optimize: 1) Adjust thread pool size, 2) Implement thumbnail priority queue, 3) Add thumbnail generation cancellation.
-[ ] NAME:Error Handling - Failed Thumbnail Generation DESCRIPTION:LOW PRIORITY: Add proper error handling: 1) Show error icon for failed thumbnails, 2) Log thumbnail generation errors, 3) Retry mechanism for transient failures.
 -[ ] NAME:Implement tags system DESCRIPTION:Add tags table to database, tag assignment UI, tag filtering, and tag management (create/rename/delete tags).
--[ ] NAME:Implement rating system DESCRIPTION:Add rating column to assets table (0-5 stars), rating UI in info panel and grid overlay, rating filter in filters panel.
--[ ] NAME:Test and fix all CRUD operations DESCRIPTION:Test folder create/rename/delete operations. Test asset move operations. Verify database updates correctly and UI refreshes properly.
--[ ] NAME:Add asset preview panel DESCRIPTION:Enhance info panel to show full-size image preview or video player for selected asset. Add metadata display (dimensions, duration, codec, etc).
+-[ ] NAME:Implement rating system DESCRIPTION:Add rating column to assets table (0-5 stars), rating UI in info panel and grid overlay, rating filter in filters panel.
+-[ ] NAME:Test and fix all CRUD operations DESCRIPTION:Test folder create/rename/delete operations. Test asset move operations. Verify database updates correctly and UI refreshes properly.
+-[ ] NAME:Add asset preview panel DESCRIPTION:Enhance info panel to show full-size image preview or video player for selected asset. Add metadata display (dimensions, duration, codec, etc).
+-[ ] NAME:Implement asset search bar DESCRIPTION:Add a search field that filters assets by filename, tags, and metadata in real time; integrate with AssetsModel filtering and highlight matches.
+-[ ] NAME:Implement collections system DESCRIPTION:Allow users to create, rename, delete collections (virtual groupings) and add/remove assets; expose in UI alongside folders and enable filtering by collection.
+-[ ] NAME:Settings panel enhancements DESCRIPTION:Convert debug Settings tab into user-facing preferences (theme, cache size, default view, shortcuts) persisted across sessions.
+-[/] NAME:Preview shortcuts DESCRIPTION:Support double-click and spacebar to open the asset preview/lightbox with keyboard navigation and close/next/previous controls.














