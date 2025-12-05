# KAsset Manager 1.4.0 Release Notes

**Release Date:** December 5, 2025

## Overview

Version 1.4.0 focuses on significant performance improvements to the File Manager, eliminating UI blocking when navigating folders and expanding the folder tree. This release delivers a much more responsive user experience when working with large directory structures.

## Major Features & Improvements

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

### 🔧 Technical Improvements

- **Lambda-based async callbacks**: Replaced `qobject_cast` pattern with lambda connections to avoid template issues
- **Mutex protection**: Added thread-safe tracking of in-flight async fetch operations
- **Smart duplicate prevention**: Prevents simultaneous fetches of the same path
- **Case-folded path keys**: Handles Windows path case-insensitivity correctly

## Bug Fixes

- Fixed UI freezes when selecting folders in File Manager
- Fixed UI hangs when expanding folder trees with many subfolders
- Eliminated blocking I/O operations on the UI thread during navigation
- Resolved paint performance issues in file grid/list views

## Performance Metrics

- **Folder navigation**: Now responsive even with thousands of subfolders
- **Tree expansion**: Typically completes without user-perceptible delay
- **UI responsiveness**: Application remains fully interactive during folder operations
- **Memory usage**: Optimized with bounded LRU cache for file type information

## Files Modified

- `src/everything_folder_model.h` - Added async infrastructure
- `src/everything_folder_model.cpp` - Implemented async enumeration pattern
- `src/mainwindow.h` - Added debounce timer and pending path tracking
- `src/mainwindow.cpp` - Integrated async signals and deferred navigation
- `src/fm_item_delegate.cpp` - Replaced disk lookups with model cache
- `native/qt6/CMakeLists.txt` - Version update to 1.4.0
- `installer/kassetmanager.nsi` - Version update to 1.4.0

## Known Limitations

- Folder enumeration performance depends on Everything SDK availability on Windows
- Very large folder trees (10,000+ subfolders) may still show minor delays on initial load
- UNC paths (network shares) may have longer enumeration times due to network latency

## Testing Recommendations

1. **Folder Navigation**: Test expanding large folder trees with many subfolders
2. **Rapid Clicking**: Verify debouncing works by rapidly clicking different folders
3. **UI Responsiveness**: Confirm application remains responsive during folder operations
4. **Memory Usage**: Monitor memory consumption during extended file manager sessions
5. **Everything SDK**: Test with and without Everything SDK installed to verify fallback behavior

## Upgrade Notes

- This is a minor version update with no breaking changes
- No database migrations required
- All previous settings and configurations are compatible
- Recommended for all users, especially those working with large folder structures

## Credits

Performance improvements powered by:

- Qt 6 (`QtConcurrent::run` and `QFutureWatcher`)
- Everything SDK for fast folder enumeration
- Efficient async/await patterns for responsive UI

---

**For more information**, visit the [Documentation](docs/) folder or check the [Developer Guide](docs/DEVELOPER_GUIDE.md).
