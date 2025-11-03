# KAssetManager v0.3.0 - Release Notes

**Release Date:** 2025-11-03  
**Branch:** feature/intelligent-features  
**Status:** Ready for Testing

---

## Overview

KAssetManager v0.3.0 introduces five intelligent features designed to enhance workflow efficiency, data integrity, and user experience. These features provide professional-grade tools for asset management, search, and maintenance.

---

## New Features

### 1. Everything Search Integration ⚡

Ultra-fast disk-wide file search with bulk import capability.

**Key Features:**
- Search millions of files in milliseconds using Everything search engine
- Dual-mode support: Asset Manager (bulk import) and File Manager (navigation)
- File type filters: Images, Videos, Audio, Documents
- Import status tracking (shows which files are already imported)
- Bulk import with progress dialog
- Network drive support (requires configuration)

**Requirements:**
- Everything (free) from https://www.voidtools.com/
- Everything64.dll (automatically installed during build)

**Usage:**
- Click search button in Asset Manager or File Manager toolbar
- Enter search query (supports wildcards: `*.jpg`, `render_*`, etc.)
- Select files and click "Import Selected" (Asset Manager) or "Select Files" (File Manager)

---

### 2. Database Health Agent 🏥

Automated health monitoring and maintenance for your asset database.

**Health Checks:**
- Orphaned records detection (assets with missing files)
- Missing files sampling (estimates percentage of missing files)
- Fragmentation analysis (database efficiency)
- SQLite integrity check (database corruption detection)
- Index validation (ensures all indexes are valid)

**Maintenance Operations:**
- **VACUUM:** Rebuild database, reclaim space, reduce fragmentation
- **REINDEX:** Rebuild all indexes for optimal query performance
- **Fix Orphaned Records:** Remove database entries for missing files

**Usage:**
- Automatic health check on startup (non-blocking)
- Manual check: **Tools → Database Health**
- Click maintenance buttons to run operations
- View detailed health report with recommendations

**Health Status:**
- **Healthy:** No issues detected
- **Warning:** Minor issues (< 5% missing files, < 30% fragmentation)
- **Critical:** Major issues (≥ 5% missing files, ≥ 30% fragmentation)

---

### 3. Bulk Rename Intelligence 📝

Pattern-based bulk renaming with live preview and rollback capability.

**Key Features:**
- Pattern-based renaming with placeholders
- Live preview before applying changes
- Rollback support (undo rename operations)
- Safety checks (prevents overwriting existing files)
- Dual-mode: Asset Manager (updates database) and File Manager (files only)

**Pattern Placeholders:**
- `{name}` - Original filename (without extension)
- `{ext}` - File extension
- `{#}` - Sequential number (1, 2, 3, ...)
- `{##}` - Zero-padded number (01, 02, 03, ...)
- `{###}` - Three-digit number (001, 002, 003, ...)

**Example Patterns:**
```
render_{###}.{ext}          → render_001.exr, render_002.exr, render_003.exr
shot_A_{name}.{ext}         → shot_A_original1.jpg, shot_A_original2.jpg
project_v{#}.{ext}          → project_v1.png, project_v2.png
{name}_final.{ext}          → image1_final.jpg, image2_final.jpg
```

**Usage:**
- Select 2+ assets/files
- Right-click → **Bulk Rename...**
- Enter pattern, review preview
- Click **Rename** to apply
- Click **Rollback** to undo (if needed)

---

### 4. Sequence Intelligence 🎬

Automatic detection of image sequences with gap detection and version tracking.

**Key Features:**
- Automatic sequence detection during import
- Gap detection (identifies missing frames)
- Version extraction (v01, v02, etc.)
- Visual warning badges on thumbnails with gaps
- Detailed gap information in info panel

**Gap Information:**
- Single gap: `Gaps: 5`
- Multiple gaps: `Gaps: 3, 7, 12`
- Range of gaps: `Gaps: 10-15, 20-25`

**Visual Indicators:**
- **⚠️ Warning badge** on thumbnails with gaps
- Hover over badge to see gap count
- Info panel shows detailed gap list and version

**Usage:**
- Import image sequences normally
- Sequences with gaps automatically detected
- View gap information in info panel
- Use Everything Search to find missing frames

---

### 5. Context Preserver 💾

Automatic UI state persistence per folder - never lose your place.

**Saved State (Per Folder):**
- Scroll position (grid and list views)
- View mode (grid or list)
- Search text
- Selected tag filters
- Rating filter
- File type filter
- Selected asset IDs

**Global State:**
- Last active folder (restored on startup)
- Window geometry and splitter positions
- Preview panel visibility

**Usage:**
- Works automatically in background
- No configuration required
- State saved when switching folders
- State restored when returning to folder
- Last active folder restored on startup

---

## Installation

### Prerequisites

1. **Everything** (for Everything Search feature):
   ```powershell
   # Download and install from https://www.voidtools.com/
   # Ensure "Install Everything Service" is checked
   ```

2. **Everything SDK** (automatically downloaded during build):
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\download-everything-sdk.ps1
   ```

### Build and Package

```powershell
# Build with all features
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package

# Run the application
.\dist\portable\bin\kassetmanagerqt.exe
```

### Verify Installation

1. **Check Everything64.dll:**
   ```powershell
   Test-Path "dist/portable/bin/Everything64.dll"
   # Should return: True
   ```

2. **Check Everything Service:**
   - Look for Everything icon in system tray
   - If not running, launch Everything from Start Menu

3. **Test Features:**
   - Click search button in Asset Manager (should open search dialog)
   - Go to **Tools → Database Health** (should open health dialog)
   - Select 2+ assets, right-click → **Bulk Rename...** (should open rename dialog)

---

## Configuration

### Everything Search - Network Drives

To search network drives, configure Everything:

**Option A: Add Specific Folders**
1. Open Everything
2. **Tools → Options → Indexes → Folders**
3. Click **Add...** and add network paths (e.g., `\\server\share`)
4. Click **OK**

**Option B: Enable All Network Drives (Everything 1.5+)**
1. Open Everything
2. **Tools → Options → Indexes**
3. Check **"Index network drives"**
4. Click **OK**

---

## Documentation

- **[INTELLIGENT_FEATURES.md](INTELLIGENT_FEATURES.md)** - Complete installation and usage guide
- **[README.md](README.md)** - Project overview and quick start
- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** - Technical documentation for developers
- **[NEW_FEATURES.md](NEW_FEATURES.md)** - Feature roadmap and implementation status

---

## Troubleshooting

### Everything Search Not Working

**Error: "Everything search engine is not available"**

1. Check Everything is running (system tray icon)
2. Verify Everything64.dll exists: `Test-Path "dist/portable/bin/Everything64.dll"`
3. Check logs: Press `Ctrl+L` to open log viewer, look for `[EverythingSearch]` messages
4. Rebuild with `-Package` flag if DLL is missing

### Database Health Issues

**Health check shows critical status:**
1. Run **VACUUM** to reduce fragmentation
2. Run **REINDEX** to rebuild indexes
3. Run **Fix Orphaned Records** to clean up missing files
4. Consider re-importing missing assets

### Bulk Rename Errors

**Error: "File already exists"**
- Modify pattern to ensure unique names
- Use `{#}` or `{##}` for sequential numbering

**Error: "Permission denied"**
- Close applications using the files
- Check file permissions

---

## Known Issues

1. **Network Drive Search:** Requires manual configuration in Everything (see Configuration section)
2. **Rollback Limitation:** Rollback only works within the same session (lost when dialog closes)
3. **Large Database Health Checks:** May take several seconds for databases with > 100K assets

---

## Performance

- **Everything Search:** Searches millions of files in milliseconds
- **Database Health Check:** 1-5 seconds for typical databases
- **Bulk Rename:** Instant preview, 1-2 seconds to apply (depends on file count)
- **Sequence Detection:** Automatic during import, minimal overhead
- **Context Preserver:** Instant save/restore, stored in Windows Registry

---

## Credits

**Developed by:** KAsset Team  
**Build System:** CMake + Ninja  
**UI Framework:** Qt 6.9.3  
**Database:** SQLite 3  
**Search Engine:** Everything by voidtools (https://www.voidtools.com/)

---

## Next Steps

1. **Test all features** in your workflow
2. **Report issues** via GitHub Issues
3. **Provide feedback** on feature usability
4. **Check roadmap** in [NEW_FEATURES.md](NEW_FEATURES.md) for upcoming features

---

## Changelog

### v0.3.0 (2025-11-03)

**Added:**
- Everything Search Integration with dual-mode support
- Database Health Agent with automated checks and maintenance
- Bulk Rename Intelligence with pattern-based renaming
- Sequence Intelligence with gap detection and version tracking
- Context Preserver with per-folder UI state persistence

**Improved:**
- Log viewer with detailed debug messages
- Error handling and user feedback
- Documentation with comprehensive guides

**Fixed:**
- Everything DLL loading and initialization
- Network drive search configuration
- Bulk rename safety checks

---

For complete documentation, see [INTELLIGENT_FEATURES.md](INTELLIGENT_FEATURES.md).

