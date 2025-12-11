# KAsset Manager 1.5.0 Release Notes

**Release Date:** December 11, 2025

## Overview

Version 1.5.0 is a major release focused on the **Project Manager** feature—a complete VFX/post-production project tracking system with version detection, folder watching, and project file support. This release also includes significant File Manager improvements and numerous quality-of-life enhancements.

---

## Major Features

### 🎬 Project Manager (New!)

A dedicated tab for managing VFX/post-production projects with intelligent version tracking:

#### Project & Watch Folder System
- **Project Creation**: Create projects from any folder—automatically imports all assets and watches for changes
- **Watch Folder Monitoring**: Real-time detection of new, modified, and deleted files across all subdirectories
- **Recursive Subdirectory Watching**: `QFileSystemWatcher` now monitors all subdirectories (not just root), ensuring changes in nested folders are detected
- **Smart Caching**: Directory modification times are cached to avoid unnecessary rescans—only changed directories are processed
- **Debounced Updates**: 2-second debounce prevents excessive rescans during bulk file operations

#### Project File Version Detection
- **After Effects Support**: Detects `.aep` and `.aepx` files, extracts version strings (e.g., "v001", "v2", "comp_v03")
- **Nuke Support**: Detects `.nk` files with version pattern extraction
- **Version Grouping**: Multiple versions of the same project file are grouped together with a clickable version badge
- **Version Dropdown**: Click the version badge to switch between versions—preview updates immediately

#### Project Manager UI
- **Grid & List Views**: Same familiar interface as File Manager and Asset Manager
- **Thumbnail Previews**: Live thumbnails for all media files with scrubbing support
- **Sequence Detection**: Image sequences are grouped and displayed as single items with frame range info
- **Notification System**: Badge shows count of new files detected since last view
- **Full Preview Panel**: Embedded preview with video playback, image zoom/pan, and metadata display

### 📁 File Manager Path Bar (New!)

An editable address bar at the top of the File Manager file panel, similar to Windows Explorer:

- **Display Current Path**: Shows the full path of the current directory
- **Selectable/Copyable**: Click and select text to copy the path
- **Direct Navigation**: Type or paste a path and press Enter to navigate
- **Path Validation**: If the entered path doesn't exist, shows an error and restores the previous path
- **File Path Handling**: If a file path is entered, navigates to its parent directory
- **Native Separators**: Displays Windows-style backslashes for familiarity

### 🪟 Window Title Version Display

- The main window title now shows the application version: "KAsset Manager 1.5.0"
- Version is sourced from CMake's `PROJECT_VERSION` via `QCoreApplication::applicationVersion()`

---

## Improvements

### Project Manager File Operations
- **Delete**: Removes files from database immediately, then moves to Recycle Bin
- **Rename**: Updates database path after successful filesystem rename
- **Cut/Paste (Move)**: Removes from database at source, watcher adds at destination
- **Copy/Paste**: Watcher detects new files at destination and adds to database

### Import System Enhancements
- **Throttled UI Updates**: Progress dialog updates every 50ms instead of every file, preventing UI freeze during large imports
- **Removed processEvents() Calls**: Eliminated reentrancy issues in `ImportProgressDialog`
- **Elapsed Timer Pattern**: Uses `QElapsedTimer` for throttling instead of per-file event processing
- **Better Logging**: Import operations now log folder counts, file counts, and sequence detection results

### Watch Folder Reliability
- **Full Subdirectory Watching**: Fixed issue where only root directory was watched—now all subdirectories are monitored
- **Proper Path Cleanup**: `unwatchProject()` now removes all watched paths for the project
- **Dynamic Directory Addition**: New subdirectories are automatically added to the watch list
- **Throttled Logging**: Directory change notifications are logged at most every 5 seconds to prevent spam

### Database Operations
- **New `removeAssetsByPath()`**: Efficiently removes multiple assets by file path with folder notification
- **New `updateAssetPath()`**: Updates asset file path in database (for rename operations)
- **Background Resync**: "Re-sync Asset Folders" now runs on a background thread using `QtConcurrent::run()`

### High DPI Support (from 1.4.5)
- Enhanced icon scaling for high-DPI displays
- Improved UI element sizing and spacing

---

## Bug Fixes

- **Watch Folder Not Detecting Changes**: Fixed `QFileSystemWatcher` only watching root directory—now watches all subdirectories recursively
- **Resync Freezing UI**: Re-sync operation now runs on background thread
- **File Operations Not Updating Database**: Delete, rename, and move operations now properly update the Project Manager database
- **Sequence Frame Path Reconstruction**: Fixed pattern-based frame path reconstruction using `filePath` (full path) instead of just the pattern filename
- **Version Badge Click Detection**: Moved click handling to `MainWindow::eventFilter` for more reliable detection
- **Import Progress Dialog Reentrancy**: Removed `processEvents()` calls that could cause crashes during import

---

## Technical Changes

### New Files
- `project_import_worker.h/.cpp` - Background thread worker for project imports with cancellation support

### Modified Core Files
- `mainwindow.cpp/.h` - Added path bar, version dropdown handling, file operation database updates
- `project_db.cpp/.h` - Added `removeAssetsByPath()`, `updateAssetPath()` methods
- `project_manager_watcher.cpp/.h` - Complete rewrite for recursive watching and smart caching
- `project_item_delegate.cpp/.h` - Added `setSelectedVersions()` and `isPointOnVersionBadge()` methods
- `importer.cpp` - Added throttled progress updates with `QElapsedTimer`
- `import_progress_dialog.cpp` - Removed `processEvents()` calls to fix reentrancy

### CMake Changes
- Added `project_import_worker.h/.cpp` to build
- Version bumped to 1.5.0

---

## Migration Notes

- **Database**: No schema changes—existing databases work without modification
- **Settings**: All settings are preserved from previous versions
- **Project Database**: Stored separately from Asset Manager database in `%AppData%/KAsset/KAsset Manager Qt/projects.db`

---

## Known Issues

- Version detection works best with standard naming conventions (v001, v1, _v02, etc.)
- Very large projects (100k+ files) may take several seconds for initial import

---

## Acknowledgments

Thanks to everyone who provided feedback and reported issues for this release!
