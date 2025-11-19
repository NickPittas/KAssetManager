# Project Folders Feature Implementation

## Overview
Implemented a comprehensive project folder watchdog system that allows users to add external folders to the asset manager, monitor them for changes, and control asset movement with a locking mechanism.

## Features Implemented

### 1. Project Folder Management
- **Add Project Folder**: Users can add external folders as "Project Folders" via File menu (Ctrl+P)
- **Special Icon**: Project folders are displayed with a special icon in the folder tree (distinct from regular folders)
- **Separate from Regular Folders**: Project folders are created at the root level and cannot be moved
- **Rename Support**: Project folder names can be changed via context menu
- **Remove Support**: Project folders can be removed (removes from library, doesn't delete files)

### 2. File System Watchdog
- **Automatic Monitoring**: All project folders are automatically monitored for file system changes
- **Subdirectory Watching**: Automatically watches all subdirectories within project folders
- **Debounced Refresh**: Changes are debounced (500ms) to avoid excessive refreshes
- **Change Detection**: Detects new files, modified files, and new subdirectories

### 3. Asset Locking System
- **Lock Checkbox**: Red "🔒 Lock Assets" checkbox in the toolbar (checked by default)
- **Movement Restrictions**: When locked, assets can only be moved within their project folder
- **Cross-Project Prevention**: Prevents moving assets between different project folders
- **Visual Feedback**: Clear warning messages when attempting restricted operations
- **Unlock Option**: Users can uncheck the lock to allow free movement of assets

### 4. Refresh Functionality
- **Manual Refresh Button**: "🔄 Refresh" button in toolbar to manually refresh all project folders
- **Automatic Refresh**: Automatic refresh when file system changes are detected
- **Re-import on Refresh**: Re-imports folder contents to pick up new/changed files

## Database Schema Changes

### New Table: `project_folders`
```sql
CREATE TABLE project_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE,
    path TEXT NOT NULL UNIQUE,
    virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP
);
```

### New DB Methods
- `createProjectFolder(name, path)` - Create a new project folder
- `renameProjectFolder(id, name)` - Rename a project folder
- `deleteProjectFolder(id)` - Remove a project folder
- `listProjectFolders()` - Get all project folders
- `getProjectFolderPath(id)` - Get path for a project folder
- `getProjectFolderIdByVirtualFolderId(virtualFolderId)` - Find project folder by virtual folder ID

## New Files Created

### 1. `project_folder_watcher.h/cpp`
- `ProjectFolderWatcher` class using `QFileSystemWatcher`
- Monitors project folders and subdirectories
- Emits signals when changes are detected
- Debouncing mechanism to prevent excessive refreshes

### 2. Database Extensions
- Extended `db.h/cpp` with project folder operations
- New signal: `projectFoldersChanged()`

### 3. UI Extensions
- Extended `mainwindow.h/cpp` with:
  - Lock checkbox control
  - Refresh button
  - Project folder management slots
  - Asset movement restriction logic

### 4. Folder Model Extensions
- Extended `virtual_folders.h/cpp` with:
  - `isProjectFolder` flag in `VFNode` structure
  - `projectFolderId` field in `VFNode` structure
  - New roles: `IsProjectFolderRole`, `ProjectFolderIdRole`
  - Icon support for project folders

## User Interface Changes

### Toolbar Additions
1. **Lock Checkbox** (right side of toolbar)
   - Red color when checked
   - Tooltip: "When locked, assets can only be moved within their project folder"
   - Default: Checked (locked)

2. **Refresh Button** (right side of toolbar)
   - Icon: 🔄
   - Tooltip: "Refresh assets from project folders"
   - Manually triggers refresh of all project folders

### Menu Additions
- **File → Add Project Folder** (Ctrl+P)
  - Opens folder selection dialog
  - Prompts for project folder name
  - Optionally imports assets immediately

### Context Menu Changes
- **Folder Context Menu**:
  - Project folders show "Remove Project Folder" instead of "Delete"
  - Rename works for both regular and project folders
  - Cannot move project folders

## Behavior Details

### Asset Movement Restrictions (When Locked)
1. **Context Menu "Move to Folder"**:
   - Checks if source and target are in same project folder
   - Shows warning if attempting cross-project move
   - Allows movement within same project folder

2. **Drag and Drop**:
   - Same restrictions as context menu
   - Visual feedback via warning dialog
   - Drop is rejected if move is not allowed

3. **When Unlocked**:
   - All restrictions are removed
   - Assets can be moved freely between any folders
   - Status bar shows "Assets unlocked - can move freely"

### Watchdog Behavior
1. **On File/Directory Change**:
   - Detects changes in monitored folders
   - Adds new subdirectories to watch list
   - Triggers debounced refresh (500ms delay)

2. **On Refresh**:
   - Re-imports entire project folder
   - Updates existing assets
   - Adds new assets
   - Status bar shows progress

## Usage Workflow

### Adding a Project Folder
1. File → Add Project Folder (or Ctrl+P)
2. Select folder from file dialog
3. Enter a name for the project folder
4. Choose whether to import assets now
5. Folder appears in tree with special icon
6. Watchdog automatically monitors the folder

### Working with Project Folders
1. **Locked Mode (Default)**:
   - Assets stay within their project folder
   - Organize assets within project boundaries
   - Safe from accidental cross-project moves

2. **Unlocked Mode**:
   - Uncheck "🔒 Lock Assets"
   - Move assets freely between projects
   - Useful for reorganization

3. **Refreshing**:
   - Click "🔄 Refresh" to manually update
   - Or wait for automatic detection (500ms after changes)

### Removing a Project Folder
1. Right-click project folder in tree
2. Select "Remove Project Folder"
3. Confirm removal
4. Folder and assets removed from library (files not deleted)
5. Watchdog stops monitoring

## Technical Implementation Notes

### Thread Safety
- File system watcher runs on Qt's event loop
- Debouncing prevents race conditions
- Database operations are synchronous

### Performance
- Subdirectories are watched recursively
- Debouncing reduces unnecessary refreshes
- Only changed project folders are refreshed

### Error Handling
- Validates folder paths before adding
- Handles missing folders gracefully
- Shows user-friendly error messages
- Logs all operations for debugging

## Testing Recommendations

1. **Add Project Folder**:
   - Add a folder with existing assets
   - Verify assets are imported
   - Check folder appears with special icon

2. **Watchdog**:
   - Add new files to project folder
   - Verify automatic refresh (wait 500ms)
   - Check new assets appear in grid

3. **Locking**:
   - Try moving asset to different project (should fail)
   - Try moving asset within same project (should work)
   - Unlock and try cross-project move (should work)

4. **Refresh**:
   - Add files manually
   - Click refresh button
   - Verify new assets appear

5. **Rename/Remove**:
   - Rename project folder
   - Verify name updates in tree
   - Remove project folder
   - Verify assets are removed from library

## Future Enhancements (Not Implemented)

- Project folder icons (currently using Qt default icon)
- Per-project folder settings
- Selective folder watching (enable/disable per folder)
- Import filters (file types, size limits)
- Conflict resolution for duplicate files
- Project folder groups/categories

