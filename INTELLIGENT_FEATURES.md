# KAssetManager - Intelligent Features Guide

**Version:** 0.3.0  
**Last Updated:** 2025-11-03  
**Status:** Production Ready

This guide covers the installation, configuration, and usage of the intelligent features implemented in KAssetManager v0.3.0.

---

## Table of Contents

1. [Overview](#overview)
2. [Feature 3: Everything Search Integration](#feature-3-everything-search-integration)
3. [Feature 6: Database Health Agent](#feature-6-database-health-agent)
4. [Feature 7: Bulk Rename Intelligence](#feature-7-bulk-rename-intelligence)
5. [Feature 9: Sequence Intelligence](#feature-9-sequence-intelligence)
6. [Feature 10: Context Preserver](#feature-10-context-preserver)
7. [Troubleshooting](#troubleshooting)

---

## Overview

KAssetManager v0.3.0 introduces five intelligent features designed to enhance workflow efficiency, data integrity, and user experience:

| Feature | Description | Availability |
|---------|-------------|--------------|
| **Everything Search** | Ultra-fast disk-wide search with bulk import | Asset Manager + File Manager |
| **Database Health Agent** | Automated health checks and maintenance | Asset Manager |
| **Bulk Rename Intelligence** | Pattern-based renaming with preview | Asset Manager + File Manager |
| **Sequence Intelligence** | Gap detection and version tracking | Asset Manager |
| **Context Preserver** | Per-folder UI state persistence | Asset Manager + File Manager |

---

## Feature 3: Everything Search Integration

### Overview

Everything Search Integration provides lightning-fast disk-wide file search using the Everything search engine. Search millions of files in milliseconds and bulk import results directly into your asset library.

### Prerequisites

**Required:**
1. **Everything** (free) - Download from https://www.voidtools.com/
2. **Everything SDK** - Automatically downloaded during build via `scripts/download-everything-sdk.ps1`

**Installation Steps:**

1. **Install Everything:**
   ```powershell
   # Download and install Everything from https://www.voidtools.com/
   # Run the installer and ensure "Install Everything Service" is checked
   ```

2. **Verify Everything is Running:**
   - Check your system tray for the Everything icon
   - If not running, launch Everything from Start Menu

3. **Configure Network Drive Indexing (Optional):**
   
   Everything by default only indexes local NTFS drives. To search network drives:
   
   **Option A: Add Specific Network Folders**
   1. Open Everything
   2. Go to **Tools → Options** (or press `Ctrl+P`)
   3. Go to **Indexes → Folders** tab
   4. Click **Add...** and add your network paths (e.g., `\\server\share` or `Z:\`)
   5. Click **OK** and let Everything index those folders
   
   **Option B: Enable All Network Drives (Everything 1.5+)**
   1. Open Everything
   2. Go to **Tools → Options → Indexes**
   3. Check **"Index network drives"** or **"Index mapped network drives"**
   4. Click **OK**

4. **Verify Everything64.dll is Installed:**
   ```powershell
   # The DLL should be in your application directory
   Test-Path "dist/portable/bin/Everything64.dll"
   # Should return: True
   ```

### Usage

#### Asset Manager Mode

**Purpose:** Search entire disk for files and bulk import into your library.

**How to Use:**

1. Click the **Search** button in the Asset Manager toolbar (magnifying glass icon)
2. Enter your search query:
   - `*.jpg` - Find all JPG files
   - `render_*` - Find files starting with "render_"
   - `project_v* .exr` - Find EXR files with "project_v" in name
3. Apply filters (optional):
   - **File Type:** All Files, Images, Videos, Audio, Documents
   - **Match Case:** Enable for case-sensitive search
4. Click **Search** or press Enter
5. Review results:
   - **Status Column:** Shows "Imported" or "Not Imported"
   - **Name, Directory, Size, Modified, Type** columns
6. Select files to import (Ctrl+Click for multi-select)
7. Click **Import Selected** to bulk import non-imported files
8. Progress dialog shows import status

**Tips:**
- Use wildcards: `*` (any characters), `?` (single character)
- Combine terms: `render_v* .exr` searches for EXR files with "render_v"
- Double-click a result to open its location in File Manager
- Right-click → **Open Location** to navigate to file

#### File Manager Mode

**Purpose:** Search entire disk and navigate to file locations.

**How to Use:**

1. Click the **Search** button in the File Manager toolbar
2. Enter search query and apply filters
3. Click **Search**
4. Select files (Ctrl+Click for multi-select)
5. Click **Select Files** to navigate to the first selected file's directory
6. Or double-click a result to open its location

**Tips:**
- Use this to quickly find files anywhere on your system
- Navigate to file locations without manually browsing folders
- Combine with File Manager's "Add to Library" to import folders

### Performance

- **Speed:** Searches millions of files in milliseconds
- **Index:** Everything maintains a real-time index of all files
- **Memory:** Minimal overhead (~10-20MB for Everything service)
- **Network:** Network drive indexing may be slower depending on connection speed

### Troubleshooting

**Error: "Everything search engine is not available"**

1. **Check Everything is Running:**
   - Look for Everything icon in system tray
   - If not running, launch Everything from Start Menu

2. **Check Everything64.dll:**
   ```powershell
   Test-Path "dist/portable/bin/Everything64.dll"
   ```
   - If False, rebuild with `-Package` flag:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
   ```

3. **Check Logs:**
   - Press `Ctrl+L` to open Log Viewer
   - Look for `[EverythingSearch]` messages
   - Check which paths were tried and what errors occurred

4. **Manual DLL Installation:**
   ```powershell
   # Download Everything SDK
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\download-everything-sdk.ps1
   
   # Copy DLL to application directory
   Copy-Item "third_party/everything/Everything64.dll" "dist/portable/bin/"
   ```

**Network drives not showing in results:**
- See "Configure Network Drive Indexing" in Prerequisites section above
- Everything must be configured to index network drives

---

## Feature 6: Database Health Agent

### Overview

Database Health Agent performs automated health checks on your asset database, detects issues, and provides maintenance operations to keep your database in optimal condition.

### Features

**Health Checks:**
- **Orphaned Records:** Detects assets in database with missing files
- **Missing Files:** Samples database to estimate missing file percentage
- **Fragmentation:** Analyzes database fragmentation level
- **SQLite Integrity:** Runs SQLite's built-in integrity check
- **Index Validation:** Verifies all database indexes are valid

**Maintenance Operations:**
- **VACUUM:** Rebuilds database to reclaim space and reduce fragmentation
- **REINDEX:** Rebuilds all indexes for optimal query performance
- **Fix Orphaned Records:** Removes database entries for missing files

### Usage

#### Automatic Health Checks

Health checks run automatically on application startup:

1. Launch KAssetManager
2. Health check runs in background (non-blocking)
3. If issues detected, notification appears in status bar
4. Click notification to view detailed report

#### Manual Health Checks

1. Go to **Tools → Database Health**
2. Click **Run Health Check**
3. Wait for analysis to complete (may take a few seconds for large databases)
4. Review health report:
   - **Overall Status:** Healthy, Warning, or Critical
   - **Issue Details:** Specific problems found
   - **Recommendations:** Suggested actions

#### Maintenance Operations

1. Open Database Health dialog (**Tools → Database Health**)
2. Run health check to identify issues
3. Click maintenance buttons as needed:
   - **VACUUM Database:** Reclaim space and reduce fragmentation
   - **REINDEX Database:** Rebuild indexes for better performance
   - **Fix Orphaned Records:** Remove entries for missing files
4. Confirm operation in dialog
5. Wait for completion (progress shown in dialog)

### Health Status Indicators

| Status | Description | Action |
|--------|-------------|--------|
| **Healthy** | No issues detected | No action needed |
| **Warning** | Minor issues (< 5% missing files, < 30% fragmentation) | Consider maintenance |
| **Critical** | Major issues (≥ 5% missing files, ≥ 30% fragmentation) | Run maintenance immediately |

### Best Practices

- **Run VACUUM** after deleting many assets (reclaims disk space)
- **Run REINDEX** if queries become slow (rebuilds indexes)
- **Fix Orphaned Records** regularly to keep database clean
- **Check health** before major operations (imports, migrations)
- **Backup database** before running maintenance operations

### Performance

- **Health Check:** 1-5 seconds for typical databases (< 100K assets)
- **VACUUM:** 5-30 seconds depending on database size
- **REINDEX:** 2-10 seconds depending on number of indexes
- **Fix Orphaned:** 1-5 seconds depending on number of orphaned records

---

## Feature 7: Bulk Rename Intelligence

### Overview

Bulk Rename Intelligence provides pattern-based renaming with live preview, rollback capability, and safety checks. Rename multiple files at once using flexible patterns.

### Features

- **Pattern-Based Renaming:** Use placeholders for dynamic naming
- **Live Preview:** See results before applying changes
- **Rollback:** Undo rename operations if needed
- **Safety Checks:** Prevents overwriting existing files
- **Dual Mode:** Works in both Asset Manager and File Manager

### Usage

#### Asset Manager Mode

**Purpose:** Rename assets and update database records.

1. Select multiple assets (2 or more)
2. Right-click → **Bulk Rename...**
3. Enter rename pattern using placeholders:
   - `{name}` - Original filename (without extension)
   - `{ext}` - File extension
   - `{#}` - Sequential number (1, 2, 3, ...)
   - `{##}` - Zero-padded number (01, 02, 03, ...)
   - `{###}` - Three-digit number (001, 002, 003, ...)
4. Review preview table:
   - **Original Name:** Current filename
   - **New Name:** Proposed filename
   - **Status:** OK or error message
5. Check **Update Database** (recommended)
6. Click **Rename** to apply changes
7. If needed, click **Rollback** to undo

**Example Patterns:**

```
render_{###}.{ext}          → render_001.exr, render_002.exr, render_003.exr
shot_A_{name}.{ext}         → shot_A_original1.jpg, shot_A_original2.jpg
project_v{#}.{ext}          → project_v1.png, project_v2.png
{name}_final.{ext}          → image1_final.jpg, image2_final.jpg
```

#### File Manager Mode

**Purpose:** Rename files on disk (no database update).

1. Select multiple files (2 or more) in File Manager
2. Right-click → **Bulk Rename...**
3. Enter rename pattern (same placeholders as Asset Manager)
4. Review preview
5. Click **Rename** to apply changes
6. If needed, click **Rollback** to undo

**Note:** File Manager mode only renames files on disk. It does not update the Asset Manager database.

### Safety Features

- **Conflict Detection:** Warns if new name already exists
- **Extension Preservation:** Automatically preserves file extensions
- **Preview Before Apply:** See all changes before committing
- **Rollback Support:** Undo rename operations
- **Transaction Safety:** All renames succeed or all fail (atomic operation)

### Troubleshooting

**Error: "File already exists"**
- A file with the new name already exists in the directory
- Modify your pattern to ensure unique names
- Use `{#}` or `{##}` for sequential numbering

**Error: "Permission denied"**
- File is locked by another application
- Close applications that might be using the file
- Check file permissions

**Rollback not working:**
- Rollback only works within the same session
- If you close the dialog, rollback history is lost
- Always verify preview before applying changes

---

## Feature 9: Sequence Intelligence

### Overview

Sequence Intelligence automatically detects image sequences, identifies missing frames (gaps), and extracts version information. Visual indicators help you quickly spot incomplete sequences.

### Features

- **Gap Detection:** Identifies missing frames in sequences
- **Version Tracking:** Extracts version numbers (v01, v02, etc.)
- **Visual Indicators:** Warning badges on thumbnails with gaps
- **Info Panel:** Detailed gap information in asset info panel

### How It Works

**Automatic Detection:**
1. When importing files, KAssetManager detects image sequences
2. Analyzes frame numbers to identify gaps
3. Extracts version information from filenames
4. Stores gap and version data in database

**Sequence Patterns:**
- `render_0001.exr, render_0002.exr, render_0004.exr` → Gap at frame 3
- `shot_v01_0001.jpg, shot_v01_0002.jpg` → Version v01
- `comp_v02_####.exr` → Version v02 sequence

### Usage

#### Viewing Sequence Information

1. **Grid View:**
   - Sequences with gaps show a **⚠️ warning badge** on thumbnail
   - Hover over badge to see gap count

2. **Info Panel:**
   - Select a sequence asset
   - Info panel shows:
     - **Version:** Extracted version number (e.g., "v01")
     - **Gaps:** List of missing frames (e.g., "3, 7, 12-15")
     - **Frame Range:** Start and end frames

3. **List View:**
   - Sequences grouped together (if grouping enabled)
   - Gap information visible in info panel when selected

#### Finding Sequences with Gaps

1. Use search to filter by sequence name
2. Look for warning badges in grid view
3. Select sequence to view detailed gap information
4. Use info panel to identify specific missing frames

### Gap Information Format

**Single Gap:**
```
Gaps: 5
```

**Multiple Gaps:**
```
Gaps: 3, 7, 12
```

**Range of Gaps:**
```
Gaps: 10-15, 20-25
```

### Best Practices

- **Check gaps before rendering:** Ensure all frames are present
- **Re-import missing frames:** Use Everything Search to find missing files
- **Version tracking:** Use consistent version naming (v01, v02, etc.)
- **Sequence naming:** Use consistent frame padding (####, ###, etc.)

---

## Feature 10: Context Preserver

### Overview

Context Preserver automatically saves and restores UI state per folder, including scroll position, view mode, filters, and selected assets. Never lose your place when switching between folders.

### Features

- **Per-Folder State:** Each folder remembers its own UI state
- **Scroll Position:** Restores exact scroll position
- **View Mode:** Remembers grid vs. list view
- **Filters:** Restores search text, tags, rating, file type filters
- **Selected Assets:** Restores selected assets
- **Last Active Folder:** Restores last viewed folder on startup

### What Gets Saved

**Asset Manager:**
- Current folder ID
- Scroll position (grid and list views)
- View mode (grid or list)
- Search text
- Selected tag filters
- Rating filter
- File type filter
- Selected asset IDs

**File Manager:**
- Current directory path
- Scroll position
- View mode (grid or list)
- Preview panel visibility
- Selected file paths

### Usage

**Automatic Operation:**
- Context Preserver works automatically in the background
- No configuration or user action required
- State is saved when switching folders
- State is restored when returning to a folder

**Example Workflow:**
1. Navigate to "Project A" folder
2. Switch to grid view, scroll to middle, select some assets
3. Navigate to "Project B" folder
4. Do some work in Project B
5. Navigate back to "Project A"
6. **Result:** Grid view, scroll position, and selected assets are restored

**Startup Behavior:**
- Application opens to the last active folder
- Last view mode and filters are restored
- Scroll position is restored

### Storage

- State is stored in **QSettings** (Windows Registry)
- Settings key: `AugmentCode/KAssetManager`
- Per-folder keys: `AssetManager/Folder_{id}/...`
- File Manager keys: `FileManager/...`

### Troubleshooting

**State not restoring:**
- Check that you're returning to the same folder
- Verify QSettings is working (check Windows Registry)
- Try restarting the application

**Wrong scroll position:**
- Scroll position is based on viewport size
- If window size changed, scroll position may be approximate
- Manually scroll to desired position to update saved state

**Selected assets not restoring:**
- Assets must still exist in the folder
- If assets were deleted, selection cannot be restored
- Only assets visible with current filters are restored

---

## Troubleshooting

### General Issues

**Feature not working:**
1. Check that you're using the latest build (v0.3.0+)
2. Verify feature is enabled (no disable option currently)
3. Check log viewer (`Ctrl+L`) for error messages
4. Restart the application

**Performance issues:**
1. Run Database Health check (**Tools → Database Health**)
2. Run VACUUM and REINDEX if needed
3. Reduce number of visible assets using filters
4. Close preview panel when not in use

**Log Viewer:**
- Press `Ctrl+L` to open/close log viewer
- Or go to **View → Show Log Viewer**
- Filter by log level: All, Debug, Info, Warning, Error
- Click **Clear** to clear log history
- Logs are also saved to `app.log` and `debug.log` in application directory

### Getting Help

**Log Files:**
- `dist/portable/bin/app.log` - Application log
- `dist/portable/bin/debug.log` - Debug log

**Reporting Issues:**
1. Open log viewer (`Ctrl+L`)
2. Reproduce the issue
3. Copy relevant log messages
4. Report issue with log messages and steps to reproduce

**Feature Requests:**
- See [NEW_FEATURES.md](NEW_FEATURES.md) for planned features
- Submit feature requests via GitHub Issues

---

## Summary

The intelligent features in KAssetManager v0.3.0 provide powerful workflow enhancements:

- **Everything Search:** Find files anywhere on your system in milliseconds
- **Database Health:** Keep your database in optimal condition
- **Bulk Rename:** Rename multiple files with flexible patterns
- **Sequence Intelligence:** Detect gaps and track versions automatically
- **Context Preserver:** Never lose your place when switching folders

All features work automatically with minimal configuration. For most users, simply installing Everything is the only setup required.

For questions, issues, or feature requests, please refer to the project documentation or submit a GitHub issue.


