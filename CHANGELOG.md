# Changelog

All notable changes to KAsset Manager will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Color management settings**: OCIO config picker plus default input transforms for 8-bit, 16-bit, 32-bit, and log sources
- **Searchable OCIO inputs** in the preview overlay for faster transform selection
- **Video pan/zoom parity** with images via middle-mouse panning in the tlRender viewport

### Changed
- **OCIO configuration persistence**: enable state, config path, and input defaults now survive restarts
- **OCIO input lists** now populate directly from the selected config (including ACEScc/ACEScct/ACEScg) and apply LUTs from that config
- **Preview fit behavior**: right-click returns videos to fit view; navigation to next/previous resets fit view

### Note
This file now includes the 1.8.0 release notes below. Unreleased will contain future work.

## [1.8.0] - 2026-01-13

### Added
- **tlRender (mrv2) playback engine** for video and image-sequence preview with full OpenColorIO support
- **TLRenderPlayer/TLRenderWidget** integration for OCIO-aware playback and rendering

### Changed
- **Preview overlay playback** now uses tlRender exclusively for video/sequence playback
- Updated build CMake project version, Windows resource metadata, and NSIS installer metadata to 1.8.0

### Removed
- Legacy GStreamer preview paths and runtime packaging in favor of tlRender

## [1.6.0] - 2025-12-30

### Added
- **Dual-pane File Manager: persistent split and sync enhancements**
  - Dual-pane mode now persists across restarts (enabled/disabled state and splitter sizes saved)
  - Synced navigation improvements: optional sync toggle to keep both panes in lock-step
  - `FileManagerPane` now exposes `pathForIndex()` and `setCurrentIndex()` for robust pane-aware operations
- **Full-screen Preview Improvements**
  - Preview overlay now correctly tracks which pane opened it and navigates within that pane's file list
  - Double-click and spacebar open preview from either pane and navigation (arrow/scroll) follows the originating pane
  - Sequence handling improvements: sequence playback and representative-frame navigation work from both panes
- **UI polish and toolbar consistency**
  - Toolbars and buttons standardized (40px toolbar height, 20x20 tool buttons) for consistent appearance
  - Dual-pane icon added to `icon_utils` and theme-aware styling for checked `QToolButton`

### Changed
- **Window & Splitter behavior**
  - Folder tree pane is now locked during full-window resize (it keeps the user-set width); only file panes expand/contract
  - Splitter stretch factors adjusted so primary tree does not auto-grow when resizing the app
  - Dual-pane splitter sizes are saved to settings and restored on startup
- **Tree auto-scroll suppression**
  - The folder tree will no longer auto-scroll horizontally to reveal long names; horizontal scrollbar is kept at the left edge
  - Asynchronous tree resolution still expands parents and selects the node without shifting horizontal offset
- **FFmpeg compatibility**
  - Video metadata code updated to use modern `AV_PROFILE_*` constants for newer FFmpeg headers

### Fixed
- **Preview navigation bug**: Scrolling in fullscreen preview opened from the secondary pane now shows files from the secondary pane instead of the primary
- **Resize constraints**: Removed minimum width barriers and set appropriate size policies so panes can be resized freely
- **Toolbar & button styling**: Fixed inconsistent button sizes and added checked styling to indicate toggle states

### Technical
- Refactored `mainwindow` preview overlay logic to track `fmOverlayFromSecondaryPane`, `fmOverlayCurrentIndex`, and `fmOverlaySourceView` so navigation behaves consistently
- Added persistence for `FileManager/DualPane` and `FileManager/DualPaneSplitterSizes` in `QSettings`
- Improved `fmScrollTreeToPath()` to avoid horizontal scrolling and to wait for async resolution when needed
- Updated build CMake project version and Windows resource metadata to 1.6.0

## [1.5.0] - 2025-12-11

### Added
- **Project Manager**: Complete VFX/post-production project tracking system
  - Project creation from any folder with automatic asset import
  - Real-time folder watching with recursive subdirectory monitoring
  - Smart directory caching—only rescans changed directories
  - Debounced updates (2s) prevent excessive rescans during bulk operations
- **Project File Version Detection**
  - After Effects (`.aep`, `.aepx`) support with version pattern extraction
  - Nuke (`.nk`) support with version grouping
  - Clickable version badge to switch between versions
- **File Manager Path Bar**: Editable address bar for direct navigation
  - Type or paste paths and press Enter to navigate
  - Shows current path with native Windows separators
  - Validates paths and shows errors for invalid entries
- **Window Title Version Display**: Shows "KAsset Manager 1.5.0" in title bar

### Changed
- **Import Throttling**: Progress dialog updates every 50ms instead of per-file
- **Background Resync**: "Re-sync Asset Folders" runs on background thread using `QtConcurrent::run()`
- **Removed processEvents()**: Eliminated reentrancy issues in `ImportProgressDialog`

### Fixed
- **Watch Folder Not Detecting Changes**: Now watches all subdirectories recursively (not just root)
- **Resync Freezing UI**: Operations run on background thread
- **File Operations Not Updating Database**: Delete, rename, move now update Project Manager database
- **Sequence Frame Paths**: Fixed path reconstruction using `filePath` instead of pattern-only
- **Version Badge Click Detection**: Moved to `MainWindow::eventFilter` for reliability
- **Import Progress Dialog Crashes**: Fixed reentrancy from `processEvents()` calls

### Technical
- New `project_import_worker.h/.cpp` for background imports with cancellation
- Added `ProjectDB::removeAssetsByPath()` for bulk path deletion
- Added `ProjectDB::updateAssetPath()` for rename operations
- Complete rewrite of `project_manager_watcher.cpp` for smart caching
- Added `ProjectItemDelegate::setSelectedVersions()` and `isPointOnVersionBadge()`

## [1.3.8] - 2025-11-23

### Added
- **Help Menu**: New Help menu in the menu bar with quick access to documentation and application information
  - **User Guide (F1)**: Opens embedded user guide dialog with beautifully rendered markdown documentation
  - **About KAsset Manager**: Shows application version, author, license, and GitHub repository link
- **Embedded User Guide**: Complete user documentation is now embedded in the application binary
  - **Theme-Aware Rendering**: Automatically adapts to dark/light mode with custom CSS styling
  - **Native Markdown Display**: Uses Qt's QTextBrowser with setMarkdown() for proper formatting
  - **No External Dependencies**: All 137 lines of documentation embedded as C++ raw string literal
  - **Beautiful Typography**: Custom CSS for headings, code blocks, lists, and links
- **Full-Screen Preview Navigation Enhancements**:
  - **Synchronized Selection**: When navigating assets in full-screen preview using arrow keys, the Asset Manager grid/list automatically highlights the currently previewed asset in the background
  - **Persistent Selection**: When closing the full-screen preview, the Asset Manager selection remains on the last previewed asset for immediate keyboard navigation
- **Context Menu Actions**:
  - **Show in Explorer**: Right-click any asset and select "Show in Explorer" to open Windows Explorer with that file selected
  - **Generate Thumbnail**: Right-click any asset (image, video, or sequence) to regenerate its thumbnail immediately

### Changed
- **Settings Dialog**: Version label now dynamically displays the application version from `QCoreApplication::applicationVersion()` instead of hard-coded string
- **About Dialog**: Documentation link now instructs users to press F1 instead of linking to external file

### Technical
- Created `UserGuideDialog` class (`native/qt6/src/user_guide_dialog.{h,cpp}`) with embedded markdown content
- Added Help menu to MainWindow with F1 shortcut for User Guide
- Updated `main.cpp` to set `QCoreApplication::applicationVersion()` from CMake `PROJECT_VERSION` macro
- Added compile definition `KAM_APP_VERSION="${PROJECT_VERSION}"` in CMakeLists.txt
- Implemented synchronized selection in `MainWindow::showPreview()` and `MainWindow::changePreview()`
- Added "Show in Explorer" and "Generate Thumbnail" actions to asset context menu in `MainWindow::onAssetContextMenu()`
- Updated CMake project version, Windows resources (app.rc), and NSIS installer metadata to 1.3.8
- Release artifacts now use the `KAssetManager-Setup-1.3.8.exe` naming

## [1.3.0] - 2025-11-20

### Added
- **Frame-Accurate Annotation System**: Professional annotation tools integrated into the full-screen preview overlay
  - **5 Drawing Tools**: Freehand pen, text labels, rectangles, circles/ellipses, and arrows
  - **Per-Frame Storage**: Each video/sequence frame maintains its own annotations with JSON serialization
  - **Frame-Perfect Positioning**: Explicit frame tracking prevents ±1 frame drift from GStreamer position queries
  - **Interactive Editing**: Move and resize annotations with handles, customize colors and pen widths (1-20px)
  - **Undo/Redo**: Full QUndoStack integration with Ctrl+Z/Ctrl+Y support
  - **Timeline Markers**: Visual green lines on timeline indicate annotated frames
  - **Export Options**: Save individual frames or batch export all annotated frames as PNG/JPG
  - **Keyboard Shortcuts**: `A` toggle mode, `,/.` frame step, `Del` delete annotation, `Esc` exit
  - **Icon Integration**: Custom PNG icons for all annotation tools from `Icons/Annotation/` folder

### Changed
- **OpenImageIO Unified Image Loading**: All still image formats (PNG, JPG, BMP, GIF, TIFF, EXR, HDR, PFM, PSD, etc.) are now handled exclusively by OpenImageIO for consistent quality, proper aspect ratio handling, and unified behavior across all formats. This eliminates redundancy with Qt's image reader and provides superior color management.
- **GStreamer Player Enhancement**: Added `getCurrentFrame()` method for frame capture and kept position timer running in PAUSED state for accurate timecode display during annotation mode

### Fixed
- **Grayscale Image Display**: Fixed grayscale images displaying with red tint in thumbnails and preview panes. Now properly replicates grayscale channel to all RGB channels for correct monochrome display.
- **HDR/PFM Thumbnail Generation**: Fixed HDR (.hdr) and PFM (.pfm) files not generating thumbnails in File Manager grid view. These formats are now properly recognized as previewable.
- **Video Frame Accuracy**: Fixed ±1 frame drift issue when annotating videos by implementing explicit frame tracking with proper rounding (`qRound()`) and storing frame numbers during seek/step operations

### Technical
- Modified `oiio_image_loader.cpp` to use OpenImageIO for all image formats instead of just "advanced" formats
- Added explicit channel replication logic for grayscale (1-channel) to RGB (3-channel) conversion using `channelOrder = {0, 0, 0}`
- Added "hdr" and "pfm" extensions to `isPreviewableSuffix()` in `mainwindow.cpp` to enable thumbnail generation
- Created new annotation system with 4 source files: `annotation_layer.{h,cpp}` and `annotation_items.{h,cpp}`
- Updated CMake project version, Windows resources (app.rc), and NSIS installer metadata to 1.3.0
- Release artifacts now use the `KAssetManager-Setup-1.3.0.exe` naming

## [1.2.0] - 2025-11-14

### Changed
- File Manager folder tree expansion now uses lightweight static icons and only enumerates subfolders when expanding; no file scanning or shell icon lookups occur until a folder is actually selected.
- File Manager no longer prefetches thumbnails or metadata while you scroll; per-file preview/metadata work now runs only for the currently selected item (and only when the preview and/or info panes are visible). List view columns still show basic file properties.
- The status bar live-preview progress bar label now explicitly reflects the active context, showing "File Manager previews (visible):" or "Asset previews (visible):" while previews are being generated.

### Fixed
- Severe UI stalls when expanding or browsing large network folders caused by synchronous icon and metadata lookups on the UI thread.

### Technical
- Finalised migration of all video and image sequence playback (PreviewOverlay and File Manager) to the GStreamer backend; FFmpeg is retained only for the Convert dialog/format conversion tools.
- Updated CMake project version, Windows resources, and NSIS installer metadata to 1.2.0; release artifacts now use the `KAssetManager-Setup-1.2.0.exe` naming.

## [1.1.0] - 2025-11-08

### Added
- Adaptive external drag-and-drop for image sequences that auto-detects the drop target:
  - Windows Explorer/Desktop: sends individual frame files (CF_HDROP) so file operations copy frames, not the parent folder
  - Nuke and After Effects: sends folder path(s) so they import a single sequence item

### Changed
- File Manager and Asset Manager now behave identically for external drag-and-drop

### Fixed
- Corrected brace/structure issues in preview overlay and File Manager preview drag handlers that could break builds
- Resolved cases where Nuke imported sequences as single frames instead of one sequence

### Technical
- Implemented native OLE IDataObject that adapts payload using Explorer/DCC detection:
  - Window class (CabinetWClass/WorkerW/Progman) and process name checks (explorer.exe/FileExplorer.exe)
  - Internal (in-app) drops still use application/x-kasset-sequence-urls with the full frame list
- Packaging/Installer:
  - NSIS script updated to 1.1.0 (OutFile, VIProductVersion, DisplayVersion)
  - build-installer.ps1 now uses Ninja generator, selects the newest installer artifact, and writes a matching .sha256 hash file


## [1.0.5] - 2025-11-05

### Fixed
- **Critical**: Database persistence issue where database was deleted during app updates
  - Database now stored in persistent user data location (`AppData/Roaming/KAsset/KAsset Manager Qt/`)
  - Database survives app updates and reinstallations
  - Automatic migration from old location (`appDir/data/`) to new persistent location
  - Old database preserved for safety during migration

### Changed
- Crash dump location moved to persistent user data directory
- Updated all documentation to reflect new database location

### Technical
- Changed database storage from `applicationDirPath() + "/data/"` to `QStandardPaths::AppDataLocation`
- Added migration logic in `main.cpp` to detect and copy existing databases
- Updated Windows resource file version to 1.0.5.0

## [0.2.0] - 2025-10-29

### Added
- File Manager: “Add to Library” now supports selected folders in addition to files; when folders are selected, their subfolder hierarchy is preserved and recreated in the Asset Manager (matches drag-and-drop behavior)
- File Manager: Folders-first sorting in both Grid and List views (folders always listed above files regardless of sort column or order)
- File Manager: Folder tree now stays in sync when navigating via the right-pane file view (double-clicking a folder expands/selects it in the left tree)

### Fixed
- Crash when using “Add to Library” due to passing proxy indexes to the source `QFileSystemModel`. Selection indexes are now mapped correctly, preventing hard crashes

### Improved
- Preview overlay: When closing full-size preview (Esc or close button), focus and selection return to the previously selected item (Asset Manager or File Manager) so arrow-key navigation continues immediately
- Installer/Build: Release outputs `dist/KAssetManager-Setup-0.2.0.exe` and a `.sha256` hash file

### Notes
- Scrubbable video thumbnails on hover are feasible with the current Qt/FFmpeg stack; to be implemented in a future release after perf tuning


## [0.1.0] - 2024-01-XX

### Added

#### Core Features
- **Virtual folder system** for organizing assets without moving files on disk
- **Hierarchical folder structure** with create, rename, delete, and move operations
- **Drag-and-drop import** for files and folders with recursive scanning
- **Multi-select support** with Ctrl+Click (toggle) and Shift+Click (range)
- **Tagging system** with create, rename, delete, and merge operations
- **5-star rating system** for assets
- **Search and filter** by name, tags (AND/OR mode), rating, and file type
- **Grid view** with adjustable thumbnail size (100-400px)
- **List view** with sortable columns (Name, Type, Size, Date, Rating)
- **Full-screen preview** for images, videos, and image sequences
- **Context menus** for quick access to common operations

#### Preview Features
- **Image viewer** with zoom and pan controls
- **Video player** with timeline, play/pause, and volume controls
- **Image sequence playback** at 24fps with frame navigation
- **HDR/EXR support** with color space selection (Linear, sRGB, Rec.709)
- **Keyboard navigation** with arrow keys (next/previous asset)
- **Automatic format detection** for images, videos, and sequences

#### Professional Format Support
- **OpenImageIO integration** for EXR, HDR, PSD, IFF, RAW, and 16/32-bit TIFF
- **FFmpeg backend** for video playback (MOV, MP4, AVI, MKV, WMV)
- **Image sequence detection** with automatic pattern recognition
- **Tone mapping** for HDR images with exposure control

#### Performance Optimizations
- **Multi-threaded thumbnail generation** (2-8 threads based on CPU)
- **Smart thumbnail caching** (1000-item memory cache, ~250MB)
- **Database indexes** on frequently queried columns
- **Lazy loading** for on-demand thumbnail generation
- **Optimized queries** with prepared statements

#### Database Management
- **SQLite database** with automatic migrations
- **Export database** for backups
- **Import database** to restore from backup
- **Clear database** to start fresh
- **Automatic schema updates** for new features

#### UI/UX
- **Windows Explorer-like interface** with familiar navigation
- **Three-panel layout** (folders, assets, filters/info)
- **Resizable panels** with splitters
- **Hover effects** for better visual feedback
- **Progress dialogs** for long operations
- **Info panel** showing selected asset details
- **Toolbar** with view mode toggle and thumbnail size slider

### Technical Details
- **Qt 6.9.3 Widgets** for native Windows UI
- **C++20** with modern language features
- **CMake build system** with Ninja and Visual Studio support
- **MSVC 2022** compiler
- **vcpkg** for dependency management
- **Portable deployment** with all dependencies bundled

### Known Issues
- Thumbnail generation cannot be cancelled mid-import
- No keyboard shortcuts for rating and selection
- No undo/redo functionality


- Preview navigation wraps around (no boundary check)
- Large folders (1000+ assets) may have initial load delay

## [0.0.x] - Deprecated

### QML Implementation (Abandoned)
- Initial attempt using Qt Quick/QML
- Abandoned due to unreliable mouse event handling
- Modifier keys (Ctrl/Shift) not working consistently
- Complex event propagation issues
- Migrated to Qt Widgets for reliability

---

## Version History Summary

| Version | Date | Description |
|---------|------|-------------|
| 1.3.0 | 2025-11-20 | Frame-accurate annotation system with 5 drawing tools, per-frame storage, undo/redo, timeline markers, batch export |
| 1.2.0 | 2025-11-14 | File Manager network-drive performance, selection-only metadata/previews, contextual progress bar, GStreamer-only playback |
| 1.1.0 | 2025-11-08 | Sequence-aware drag-and-drop (Explorer/Nuke/AE), file manager tree optimizations, preview pane fixes |
| 0.2.0 | 2025-10-29 | Folder-preserving Add to Library; folders-first sorting; preview focus restore; tree sync; crash fix |
| 0.1.0 | 2024-01-XX | Initial Qt Widgets release with full feature set |
| 0.0.x | 2023-12-XX | QML prototype (deprecated) |

---

## Upgrade Notes

### From 0.0.x to 0.1.0
- Complete rewrite from QML to Qt Widgets
- Database schema is compatible (no migration needed)
- Thumbnails are regenerated automatically
- All features from QML version are preserved and improved

---

## Contributing

See [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) for contribution guidelines.

---

## Support

- **User Guide**: [USER_GUIDE.md](USER_GUIDE.md)
- **Technical Docs**: [TECH.md](TECH.md)
- **API Reference**: [API_REFERENCE.md](API_REFERENCE.md)
- **GitHub Issues**: Report bugs and request features

---

## License

Proprietary - All rights reserved.

---

## Acknowledgments

- **Qt Framework** - Cross-platform UI framework
- **OpenImageIO** - Professional image I/O library
- **FFmpeg** - Video codec library
- **SQLite** - Embedded database engine

---

## Release Checklist

For maintainers preparing a release:

- [ ] Update version number in CMakeLists.txt
- [ ] Update CHANGELOG.md with release date
- [ ] Update README.md if needed
- [ ] Run full test suite
- [ ] Build release package
- [ ] Test on clean Windows installation
- [ ] Create Git tag
- [ ] Create GitHub release
- [ ] Update documentation links

---

## Future Roadmap

### Version 0.2.0 (Planned)
- Keyboard shortcuts for all operations
- Batch export with presets
- Custom metadata fields
- Smart folders with saved searches
- Improved performance for very large libraries (10,000+ assets)

### Version 0.3.0 (Planned)
- Cloud storage integration (Dropbox, Google Drive)
- Collection sharing and export
- Advanced search with query builder
- Metadata editing (EXIF, XMP, IPTC)
- Batch metadata operations

### Version 1.0.0 (Planned)
- Stable API
- Comprehensive test coverage
- Undo/redo system
- Plugin system for extensibility
- Cross-platform support (macOS, Linux)

---

## Breaking Changes

### 0.1.0
- None (initial release)

---

## Deprecations

### 0.1.0
- QML implementation completely removed
- No deprecated APIs in this release

---

## Security

### 0.1.0
- No known security vulnerabilities
- Database uses parameterized queries to prevent SQL injection
- File paths are validated before access
- No network communication (fully offline application)

---

## Performance Metrics

### 0.1.0
- **Import speed**: 4-8x faster than single-threaded (with multi-threading)
- **Database queries**: 10-100x faster with indexes
- **Thumbnail cache**: 5x larger (1000 vs 200 items)
- **Memory usage**: ~250MB for thumbnail cache (acceptable)
- **Startup time**: <2 seconds on SSD
- **Large folder load**: <1 second for 1000 assets

---

## Credits

Developed by Nick Pittas

Special thanks to:
- Qt Company for the excellent framework
- OpenImageIO contributors
- FFmpeg team
- SQLite developers
- All beta testers and early adopters

---

*Last updated: 2025-10-29*

