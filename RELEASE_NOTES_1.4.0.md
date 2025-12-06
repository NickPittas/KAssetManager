# KAsset Manager 1.4.0 Release Notes

**Release Date:** December 6, 2025

## Overview

Version 1.4.0 delivers significant performance improvements across the application, focusing on File Manager responsiveness, Asset Manager efficiency, and enhanced playback controls. This release eliminates UI blocking during folder navigation and adds professional NLE-style JKL scrubbing controls.

## Major Features & Improvements

### 🎬 JKL Scrubbing Controls (New!)

Professional NLE-style playback controls for the fullscreen preview player:

- **J key**: Play backward (reverse playback)
- **K key**: Pause
- **L key**: Play forward

Works with both videos (via GStreamer rate control) and image sequences (direction-aware frame stepping). Controls work even when media is already playing.

### 🚀 File Manager Performance Enhancements

#### Asynchronous Folder Enumeration

- **Removed UI blocking**: Everything SDK queries now run off the UI thread using `QtConcurrent::run`
- **Non-blocking navigation**: Folder tree expansion no longer freezes the application during enumeration
- **Responsive UI**: Users can interact with the application while folder contents are being loaded
- **Technical details**: Implemented async pattern in `EverythingFolderModel::fetchChildrenAsync()` with `QFutureWatcher` callbacks

#### Fixed Path Resolution Blocking

- **Partial results immediately**: `indexForPath()` now returns results as far as it can traverse without blocking
- **Async continuation**: Unfetched nodes trigger asynchronous fetches that complete in the background
- **Smart notification**: `childrenFetched` signal notifies when data is ready for deferred tree expansion

#### Debounced Navigation

- **50ms debounce timer**: Prevents rapid successive navigation from overwhelming the system
- **Smoother experience**: Reduces system load when users quickly click multiple folders
- **Optimized performance**: Consolidates multiple rapid requests into single operations

#### Deferred Tree Expansion

- **Wait for data**: `fmScrollTreeToPath()` now waits asynchronously for child data before expanding
- **Graceful loading**: Tree expands only when data is available, preventing visual glitches
- **Pending path tracking**: Stores navigation state while async operations complete

#### Cached File Type Detection

- **Removed disk lookups**: `FmItemDelegate` now uses `QFileSystemModel::isDir()` for cached data
- **Eliminated I/O in paint**: No more `QFileInfo::isDir()` calls during item rendering
- **Faster painting**: Grid/list view rendering is now significantly faster

### ⚡ Asset Manager Performance Enhancements

#### Database-Cached File Metadata

- **New DB columns**: Added `file_type` and `last_modified` columns to assets table
- **Eliminated QFileInfo calls**: `AssetsModel::query()` now reads metadata from DB instead of disk
- **Faster folder navigation**: No more per-file disk I/O when loading asset lists
- **Automatic migration**: Existing databases are upgraded automatically

#### Optimized Cache Management

- **Fixed QCache cost calculation**: LivePreviewManager now uses actual pixmap memory size for eviction
- **Proper memory budgeting**: Cache now correctly manages ~64MB of thumbnails (256 entries × 256KB each)
- **Fixed thumbnail flickering**: Resolved aggressive cache eviction causing video thumbnail flicker
- **Memory-first lookup**: Cache checks memory before disk for faster hits

#### Reduced Paint Overhead

- **Static font caching**: `AssetItemDelegate` and `FmItemDelegate` now cache QFont objects
- **Eliminated per-paint allocations**: Fonts created once, reused on every paint call
- **Smoother scrolling**: Reduced CPU usage during grid/list view rendering

#### Other Optimizations

- **Debug logging gated**: ThumbnailCacheManager debug output disabled in release builds
- **SequenceDetector optimized**: Uses string parsing instead of QFileInfo for file extensions
- **Safer processEvents()**: Import dialogs use timeout-limited event processing to reduce reentrancy

### 🔧 Technical Improvements

- **Lambda-based async callbacks**: Replaced `qobject_cast` pattern with lambda connections to avoid template issues
- **Mutex protection**: Added thread-safe tracking of in-flight async fetch operations
- **Smart duplicate prevention**: Prevents simultaneous fetches of the same path
- **Case-folded path keys**: Handles Windows path case-insensitivity correctly
- **GStreamer playback rate**: Added `setPlaybackRate()` API for reverse playback support

## Bug Fixes

- Fixed UI freezes when selecting folders in File Manager
- Fixed UI hangs when expanding folder trees with many subfolders
- Eliminated blocking I/O operations on the UI thread during navigation
- Resolved paint performance issues in file grid/list views
- **Fixed video thumbnail flickering** in File Manager caused by cache eviction
- Fixed QFileInfo blocking calls in AssetsModel during folder navigation

## Performance Metrics

- **Folder navigation**: Now responsive even with thousands of subfolders
- **Tree expansion**: Typically completes without user-perceptible delay
- **UI responsiveness**: Application remains fully interactive during folder operations
- **Memory usage**: Optimized with bounded LRU cache (~64MB for thumbnails)
- **Asset loading**: Significantly faster with DB-cached metadata

## Files Modified

### File Manager Async
- `src/everything_folder_model.h` - Added async infrastructure
- `src/everything_folder_model.cpp` - Implemented async enumeration pattern
- `src/mainwindow.h` - Added debounce timer and pending path tracking
- `src/mainwindow.cpp` - Integrated async signals and deferred navigation
- `src/fm_item_delegate.cpp` - Replaced disk lookups with model cache, static font caching

### Asset Manager Performance
- `src/assets_model.cpp` - Read file_type/last_modified from DB instead of QFileInfo
- `src/db.cpp` - Added file_type, last_modified columns with migration and index
- `src/asset_item_delegate.cpp` - Static font caching
- `src/live_preview_manager.cpp` - Fixed cache lookup order and QCache cost calculation
- `src/thumbnail_cache_manager.cpp` - Gated debug logging with NDEBUG
- `src/sequence_detector.cpp` - Replaced QFileInfo with string parsing
- `src/import_progress_dialog.cpp` - Timeout-limited processEvents()

### JKL Scrubbing
- `src/media/gstreamer_player.h` - Added setPlaybackRate() and playbackRate() API
- `src/media/gstreamer_player.cpp` - Implemented variable rate playback with GStreamer
- `src/preview_overlay.h` - Added sequencePlayDirection for reverse sequence playback
- `src/preview_overlay.cpp` - JKL key handling, direction-aware sequence timer

### Version Updates
- `native/qt6/CMakeLists.txt` - Version update to 1.4.0
- `installer/kassetmanager.nsi` - Version update to 1.4.0

## Known Limitations

- Folder enumeration performance depends on Everything SDK availability on Windows
- Very large folder trees (10,000+ subfolders) may still show minor delays on initial load
- UNC paths (network shares) may have longer enumeration times due to network latency
- Reverse video playback (J key) depends on codec support; some formats may not support negative rates

## Testing Recommendations

1. **Folder Navigation**: Test expanding large folder trees with many subfolders
2. **Rapid Clicking**: Verify debouncing works by rapidly clicking different folders
3. **UI Responsiveness**: Confirm application remains responsive during folder operations
4. **Memory Usage**: Monitor memory consumption during extended file manager sessions
5. **Everything SDK**: Test with and without Everything SDK installed to verify fallback behavior
6. **JKL Scrubbing**: Test J/K/L keys on videos and image sequences in fullscreen preview
7. **Thumbnail Stability**: Verify video thumbnails no longer flicker in File Manager
8. **Asset Loading**: Test navigating between folders with many assets

## Upgrade Notes

- This is a minor version update with no breaking changes
- Database will be automatically migrated to add new columns (file_type, last_modified)
- All previous settings and configurations are compatible
- Recommended for all users, especially those working with large folder structures

## Credits

Performance improvements powered by:

- Qt 6 (`QtConcurrent::run` and `QFutureWatcher`)
- Everything SDK for fast folder enumeration
- GStreamer for professional video playback with rate control
- Efficient async/await patterns for responsive UI

---

**For more information**, visit the [Documentation](docs/) folder or check the [Developer Guide](docs/DEVELOPER_GUIDE.md).
