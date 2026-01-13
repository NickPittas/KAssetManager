# KAsset Manager Codemap

This file summarizes the main modules of the Qt 6 desktop application and points to their primary classes and source files.
It is organized by feature rather than by directory tree.
Key UI subsystems:
- **Asset Manager** – virtual folders, asset grid/list, filters, and tagging.
- **File Manager** – filesystem tree plus grid/list with sequence grouping and shell-style file operations.
- **Preview & Playback** - LivePreviewManager + PreviewOverlay + TLRenderPlayer for video and image sequence playback.

## 1. Application shell
- **Entry point**
  - `native/qt6/src/main.cpp` – creates `QApplication`, installs `LogManager`, initializes `DB`, and shows `MainWindow`.
- **Main window and wiring**
  - `MainWindow` – `native/qt6/src/mainwindow.{h,cpp}`; owns the Asset Manager and File Manager panes, menus, toolbars, and connects signals between models, views, preview, import, conversion, health checks, and logging.
- **Theme / look & feel**
  - `ThemeManager` – `native/qt6/src/theme_manager.{h,cpp}`; centralizes palette, icons, and style tweaks used by `MainWindow` and dialogs.
- **Windows resources (legacy)**
  - `app.rc` – `native/qt6/src/app.rc`; legacy Windows resource script (not currently referenced from CMake); see `docs/CLEANUP_CANDIDATES.md`.

## 2. Database layer
- **Core database access**
  - `DB` – `native/qt6/src/db.{h,cpp}`; singleton wrapping the SQLite connection, migrations, CRUD for virtual folders, assets, tags, and integrity helpers.
- **Progress and logging helpers for DB work**
  - `ProgressManager` – `native/qt6/src/progress_manager.{h,cpp}`; tracks long-running tasks (imports, conversions, scans) for UI progress display.
  - `LogManager` – `native/qt6/src/log_manager.{h,cpp}`; installs a global Qt message handler, writes logs to file, and feeds `LogViewerWidget`.

## 3. Data models (Asset Manager + File Manager)
- **Virtual folders and tags**
  - `VirtualFolderTreeModel` – `native/qt6/src/virtual_folders.{h,cpp}`; exposes the virtual folder hierarchy shown in the Asset Manager tree.
  - `TagsModel` – `native/qt6/src/tags_model.h`; tag list model used by filters and tag editors.
- **Assets list/grid models**
  - `AssetsModel` – `native/qt6/src/assets_model.{h,cpp}`; main asset list model for the selected virtual folder, integrates with `LivePreviewManager` and `DB`.
  - `AssetsTableModel` – `native/qt6/src/assets_table_model.h`; tabular view over assets (used by auxiliary views and tools).
- **Sequence grouping proxies**
  - `SequenceGroupingProxyModel` – `native/qt6/src/sequence_grouping_proxy_model.{h,cpp}`; groups filesystem sequences in the File Manager.
  - `AssetSequenceGroupingProxyModel` – `native/qt6/src/asset_sequence_grouping_proxy_model.{h,cpp}`; groups asset sequences in the Asset Manager grid/list.

## 4. Asset Manager UI
- **Grid view and delegates**
  - `AssetGridView` – `native/qt6/src/asset_grid_view.{h,cpp}`; QListView-based grid for asset thumbnails with multi-select, drag-and-drop, and context menus.
  - `AssetItemDelegate` – `native/qt6/src/asset_item_delegate.{h,cpp}`; paints asset cells (thumbnail, name, metadata, rating) and integrates live preview scrubbing.
  - `StarRatingWidget` – `native/qt6/src/star_rating_widget.{h,cpp}`; small widget used for rating display/edit within asset views.
  - `GridScrubController` – `native/qt6/src/grid_scrub.{h,cpp}`; manages hover/drag scrubbing behaviour over grid thumbnails.
- **Dialogs and settings**
  - `ImportProgressDialog` – `native/qt6/src/import_progress_dialog.{h,cpp}`; modal dialog showing per-asset import progress and errors.
  - `SettingsDialog` – `native/qt6/src/settings_dialog.{h,cpp}`; application settings (paths, preview behaviour, keyboard shortcuts where applicable).
  - `UserGuideDialog` – `native/qt6/src/user_guide_dialog.{h,cpp}`; embedded user guide viewer with theme-aware markdown rendering; displays comprehensive documentation without external file dependencies.
  - `BulkRenameDialog` – `native/qt6/src/bulk_rename_dialog.{h,cpp}`; batch renaming tool for assets/files with preview of name changes.

## 5. File Manager UI and file operations
- **File Manager views and delegates**
  - `FmGridViewEx`, `FmListViewEx` – `native/qt6/src/fm_views_ex.{h,cpp}`; Explorer-style grid and list views for the filesystem side, with custom selection, keyboard shortcuts, and drag-and-drop.
  - `FmItemDelegate` – `native/qt6/src/fm_item_delegate.{h,cpp}`; paints file items in File Manager views, including sequence grouping indications.
- **Drag-and-drop infrastructure**
  - `VirtualDrag` – `native/qt6/src/virtual_drag.{h,cpp}`; encapsulates drag payloads for internal/external DnD (Explorer, Nuke, After Effects).
  - `DragUtils` – `native/qt6/src/drag_utils.{h,cpp}`; helpers for building drag mime data and enforcing DnD policies shared across Asset and File Manager.
  - `ContextPreserver` – `native/qt6/src/context_preserver.{h,cpp}`; remembers selection and scroll position across view refreshes.
- **Filesystem operations**
  - `FileOps` – `native/qt6/src/file_ops.{h,cpp}`; wraps Windows Shell copy/move/delete operations and Recycle Bin integration.
  - `FileOpsDialog` – `native/qt6/src/file_ops_dialog.{h,cpp}`; UI queue showing background file operations progress and errors.
  - `FileUtils` – `native/qt6/src/file_utils.{h,cpp}`; low-level filesystem helpers (path normalization, previewable-suffix checks, temp dirs, etc.).

## 6. Importing and project folders
- **Importer and sequence detection**
  - `Importer` – `native/qt6/src/importer.{h,cpp}`; scans folders/files, detects sequences, extracts metadata, and inserts assets into the database.
  - `SequenceDetector` – `native/qt6/src/sequence_detector.{h,cpp}`; identifies numbered file sequences (frames) and groups them logically.
- **Project folder monitoring**
  - `ProjectFolderWatcher` – `native/qt6/src/project_folder_watcher.{h,cpp}`; watches configured project folders for changes and triggers refresh/import operations.

## 7. Live preview, playback, and thumbnails
- **LivePreviewManager and decoding backends**
  - `LivePreviewManager` – `native/qt6/src/live_preview_manager.{h,cpp}`; singleton responsible for asynchronous preview decoding, caching, and cache eviction for stills, video, and image sequences.
  - `TLRenderPlayer` - `native/qt6/src/media/tlrender_player.{h,cpp}`; tlRender-based playback engine used by `PreviewOverlay` and `LivePreviewManager` for video/sequence playback.
  - `OIIOImageLoader` – `native/qt6/src/oiio_image_loader.{h,cpp}`; OpenImageIO-based still-image loader for high-quality image decoding and color management.
  - `VideoMetadata` – `native/qt6/src/video_metadata.{h,cpp}`; extracts and caches video metadata (dimensions, duration, FPS, codecs) used by previews and thumbnails.
- **Thumbnail caching and generation**
  - `ThumbnailCacheManager` – `native/qt6/src/thumbnail_cache_manager.{h,cpp}`; manages on-disk and in-memory thumbnail cache entries.
  - `ThumbnailGeneratorWorker` - `native/qt6/src/thumbnail_generator_worker.{h,cpp}`; worker that generates thumbnails via tlRender/OIIO on background threads.
  - `ThumbnailGeneratorDialog` – `native/qt6/src/thumbnail_generator_dialog.{h,cpp}`; UI for bulk thumbnail generation over projects or folders.

## 8. Preview overlay window and annotation system
- **PreviewOverlay and core playback**
  - `PreviewOverlay` – `native/qt6/src/preview_overlay.{h,cpp}`; dedicated resizable preview window with transport controls, scrubbing, zoom/pan, color-space selection, sequence playback, and integrated annotation mode.
  - `OfficePreview` – `native/qt6/src/office_preview.{h,cpp}`; helper used by `PreviewOverlay` to display non-media documents (PDF / Office) when supported.
- **Annotation system**
  - `AnnotationLayer` – `native/qt6/src/annotation_layer.{h,cpp}`; manages annotation mode, drawing tools (pen, text, rectangle, ellipse, arrow), undo/redo stack (QUndoStack), and scene integration.
  - `AnnotationItem` and derived classes – `native/qt6/src/annotation_items.{h,cpp}`; base annotation item class with 5 concrete implementations:
    - `TextAnnotation` – text labels with font/size/color
    - `FreehandAnnotation` – freehand drawing paths with QPainterPath
    - `RectangleAnnotation` – rectangle/square shapes
    - `EllipseAnnotation` – circle/ellipse shapes
    - `ArrowAnnotation` – arrows with directional heads
  - All annotation items support JSON serialization/deserialization, interactive editing with resize handles, and per-frame storage in `PreviewOverlay::frameAnnotations` map.
  - Frame-accurate positioning uses explicit frame tracking (`PreviewOverlay::lastKnownVideoFrame`) to prevent +/-1 frame drift from backend position queries.
  - Timeline markers (green lines) indicate which frames contain annotations.
  - Export workflow captures frame + annotations composite as PNG/JPG.

## 9. Search and Everything integration
- **Everything SDK integration**
  - `EverythingSearch` – `native/qt6/src/everything_search.{h,cpp}`; thin wrapper around Everything64.dll for fast filesystem queries.
  - `EverythingFolderModel` – `native/qt6/src/everything_folder_model.{h,cpp}`; model that exposes Everything search results as a folder tree for File Manager.
  - `EverythingSearchDialog` – `native/qt6/src/everything_search_dialog.{h,cpp}`; UI dialog for constructing/running Everything searches and navigating results.

## 10. Diagnostics, health checks, and logging UI
- **Database health**
  - `DatabaseHealthAgent` – `native/qt6/src/database_health_agent.{h,cpp}`; runs database integrity checks, orphan detection, and maintenance tasks in the background.
  - `DatabaseHealthDialog` – `native/qt6/src/database_health_dialog.{h,cpp}`; presents health-check results and maintenance options to the user.
- **Logging UI**
  - `LogViewerWidget` – `native/qt6/src/log_viewer_widget.{h,cpp}`; in-app log viewer used by `MainWindow` to display log files from `LogManager`.

## 11. Conversion tools
- **Media conversion (FFmpeg + ImageMagick)**
  - `MediaConvertDialog` – `native/qt6/src/media_convert_dialog.{h,cpp}`; UI for converting selected assets/files using FFmpeg and ImageMagick backends.
  - `MediaConverterWorker` – `native/qt6/src/media_converter_worker.{h,cpp}`; background worker that runs FFmpeg/ImageMagick commands and reports per-file progress and errors.

## 12. Shared utilities and helpers
- **Utility helpers**
  - `Utils` – `native/qt6/src/utils.{h,cpp}`; shared Qt/C++ helpers (timing, string/path helpers, RAII utilities) used across modules.
  - `IconUtils` – `native/qt6/src/icon_utils.{h,cpp}`; central icon loading and caching for consistent toolbar/menu icons.

## 13. Tests, build scripts, and third-party runtimes
- **Unit tests**
  - `native/qt6/tests` – CTest-based unit tests for DB, models, importer, sequence detector, utils, and `LivePreviewManager` (see `native/qt6/tests/CMakeLists.txt`).
- **Build and packaging scripts**
  - `scripts/build-windows.ps1` – canonical Windows build + packaging entry point (used by `docs/INSTALL.md`).
  - `scripts/build-installer.ps1` – helper for NSIS/ZIP installer creation on top of the packaged portable build.
  - `scripts/download-everything-sdk.ps1` – downloads the Everything SDK DLL to `third_party/everything` when missing.
- **Third-party runtimes (not edited here)**
  - `third_party/tlRender-build/install-Release` - tlRender runtime used for playback/sequence features.
  - `third_party/ffmpeg` – FFmpeg binaries/headers used only by the conversion tools.
  - `third_party/ImageMagick-*/` – portable ImageMagick used by image conversion tools.
  - `third_party/everything` – Everything64.dll + headers used by EverythingSearch integration.

