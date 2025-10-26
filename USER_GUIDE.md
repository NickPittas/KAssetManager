# KAsset Manager - User Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Getting Started](#getting-started)
3. [User Interface Overview](#user-interface-overview)
4. [Working with Folders](#working-with-folders)
5. [Importing Assets](#importing-assets)
6. [Viewing and Selecting Assets](#viewing-and-selecting-assets)
7. [Preview Mode](#preview-mode)
8. [Tagging System](#tagging-system)
9. [Rating System](#rating-system)
10. [Filtering and Searching](#filtering-and-searching)
11. [View Modes](#view-modes)
12. [Settings and Database Management](#settings-and-database-management)
13. [Keyboard Shortcuts](#keyboard-shortcuts)
14. [Tips and Best Practices](#tips-and-best-practices)
15. [Troubleshooting](#troubleshooting)

---

## Introduction

KAsset Manager is a professional Windows desktop application for organizing and managing digital assets. It provides a familiar Windows Explorer-like interface with powerful features for tagging, rating, filtering, and previewing your media files.

### Supported File Formats

**Images:**
- Standard formats: PNG, JPG, JPEG, BMP, GIF, TIFF, TIF
- Professional formats: EXR, HDR, PSD, IFF, RAW (via OpenImageIO)
- Image sequences: Numbered sequences (e.g., `frame.0001.exr`, `frame.0002.exr`)

**Videos:**
- MOV, MP4, AVI, MP5, MKV, WMV
- Multiple codecs supported via FFmpeg

**Audio:**
- MP3, WAV, OGG, FLAC

---

## Getting Started

### First Launch

1. **Launch the application** by running `kassetmanagerqt.exe`
2. **The database is created automatically** in `data/kasset.db`
3. **A "Root" folder is created** as the top-level container
4. **You're ready to import assets!**

### Basic Workflow

1. **Create folders** to organize your assets
2. **Import files** by dragging and dropping
3. **Tag and rate** your assets for easy organization
4. **Filter and search** to find what you need
5. **Preview** assets in full-screen mode

---

## User Interface Overview

The application window is divided into three main panels:

### Left Panel - Folder Tree
- **Hierarchical folder structure** for organizing assets
- **Right-click** for folder operations (Create, Rename, Delete)
- **Drag assets** onto folders to move them
- **Expandable/collapsible** tree structure

### Center Panel - Asset Grid/List
- **Grid View**: Thumbnail grid with file names
- **List View**: Table with columns (Name, Type, Size, Date, Rating)
- **Toolbar**: View mode toggle and thumbnail size slider
- **Multi-select**: Ctrl+Click, Shift+Click
- **Right-click** for asset operations

### Right Panel - Filters and Info
- **Search box**: Filter by file name
- **Rating filter**: Show assets by rating
- **Tags list**: Filter by tags (AND/OR mode)
- **File type filter**: Filter by extension
- **Info panel**: Shows selected asset details

---

## Working with Folders

### Creating Folders

**Method 1: Right-click menu**
1. Right-click on a folder in the tree
2. Select "Create Subfolder"
3. Enter the folder name
4. Press Enter

**Method 2: Context menu on Root**
1. Right-click on "Root"
2. Select "Create Subfolder"
3. Enter the folder name

### Renaming Folders

1. Right-click on a folder
2. Select "Rename Folder"
3. Enter the new name
4. Press Enter

### Deleting Folders

1. Right-click on a folder
2. Select "Delete Folder"
3. Confirm the deletion
4. **Note**: Assets in the folder are moved to Root, not deleted

### Moving Assets Between Folders

**Method 1: Drag and drop**
1. Select one or more assets in the grid
2. Drag them onto a folder in the tree
3. Release to move

**Method 2: Context menu**
1. Right-click on selected assets
2. Select "Move to Folder"
3. Choose the destination folder

---

## Importing Assets

### Drag and Drop Import

**Import Files:**
1. Select files in Windows Explorer
2. Drag them into the KAsset Manager window
3. Drop anywhere in the asset grid
4. **Progress dialog** shows import status

**Import Folders:**
1. Select folders in Windows Explorer
2. Drag them into the KAsset Manager window
3. All files in the folder are imported recursively
4. **Subfolders are created** automatically

### Import Progress

- **Progress bar** shows overall completion
- **Current file** being processed is displayed
- **Thumbnail generation** happens in background
- **Cancel button** to stop import (not yet implemented)

### Image Sequence Detection

KAsset Manager automatically detects image sequences:

**Pattern Recognition:**
- `filename.####.ext` (e.g., `render.0001.exr`)
- `filename_####.ext` (e.g., `shot_0001.png`)
- `filename####.ext` (e.g., `frame0001.jpg`)

**Sequence Handling:**
- Sequences are **grouped as a single asset**
- **First frame** is used for thumbnail
- **Frame count** is displayed in info panel
- **Preview mode** plays the sequence at 24fps

---

## Viewing and Selecting Assets

### Grid View

**Features:**
- Thumbnail previews (100-400px, adjustable)
- File name below thumbnail
- Rating stars (if rated)
- Hover effects for better visibility

**Adjusting Thumbnail Size:**
1. Use the **slider** in the toolbar
2. Range: 100px to 400px
3. Size is saved automatically

### List View

**Features:**
- **5 sortable columns**: Name, Type, Size, Date Modified, Rating
- **Click column headers** to sort
- **Compact view** for large libraries
- **All information** visible at once

**Switching Views:**
- Click the **Grid/List toggle button** in the toolbar
- Or use keyboard shortcut (not yet implemented)

### Selecting Assets

**Single Selection:**
- Click on an asset

**Multi-Selection:**
- **Ctrl+Click**: Toggle individual assets
- **Shift+Click**: Select range from last selected to clicked
- **Ctrl+A**: Select all (not yet implemented)

**Selection Info:**
- Selected count shown in info panel
- Total size of selected assets displayed

---

## Preview Mode

### Opening Preview

**Methods:**
- **Double-click** an asset
- **Right-click** and select "Preview"
- **Enter key** (not yet implemented)

### Preview Controls

**Navigation:**
- **Left Arrow**: Previous asset
- **Right Arrow**: Next asset
- **Escape**: Close preview

**Image Controls:**
- **Mouse wheel**: Zoom in/out
- **Click and drag**: Pan image
- **Fit to View**: Automatically fits image to window

**Video Controls:**
- **Play/Pause button**: Toggle playback
- **Timeline slider**: Scrub through video
- **Volume control**: Adjust audio level
- **Fullscreen**: Maximize video

**Image Sequence Controls:**
- **Play/Pause**: Play sequence at 24fps
- **Left/Right arrows**: Navigate frames
- **Frame counter**: Shows current frame

### HDR/EXR Images

**Color Space Selection:**
- **Linear**: No color correction
- **sRGB**: Standard RGB color space
- **Rec.709**: Broadcast standard
- **Dropdown menu** in preview toolbar

---

## Tagging System

### Creating Tags

**Method 1: From asset context menu**
1. Right-click on asset(s)
2. Select "Assign Tag"
3. Click "Create New Tag"
4. Enter tag name
5. Click OK

**Method 2: From tags panel**
1. Right-click in tags list (right panel)
2. Select "Create Tag"
3. Enter tag name

### Assigning Tags

1. Select one or more assets
2. Right-click and select "Assign Tag"
3. Check the tags you want to assign
4. Click OK
5. **Multiple tags** can be assigned to each asset

### Removing Tags

1. Select asset(s)
2. Right-click and select "Assign Tag"
3. Uncheck the tags you want to remove
4. Click OK

### Managing Tags

**Rename Tag:**
1. Right-click on tag in tags panel
2. Select "Rename Tag"
3. Enter new name

**Delete Tag:**
1. Right-click on tag in tags panel
2. Select "Delete Tag"
3. Confirm deletion
4. **Tag is removed from all assets**

**Merge Tags:**
1. Right-click on tag in tags panel
2. Select "Merge Tag"
3. Choose target tag
4. All assets with source tag get target tag
5. Source tag is deleted

---

## Rating System

### Setting Ratings

**Method 1: Context menu**
1. Right-click on asset(s)
2. Select "Set Rating"
3. Choose 1-5 stars or "No Rating"

**Method 2: Keyboard shortcuts** (not yet implemented)
- Press 1-5 to set rating
- Press 0 to clear rating

### Viewing Ratings

- **Grid view**: Stars shown below thumbnail
- **List view**: Rating column shows stars
- **Info panel**: Rating displayed for selected asset

---

## Filtering and Searching

### Search by Name

1. Type in the **search box** (right panel)
2. Results update as you type
3. **Case-insensitive** search
4. Searches file names only

### Filter by Rating

1. Use the **rating dropdown** (right panel)
2. Options:
   - All Ratings
   - 5 Stars
   - 4 Stars and above
   - 3 Stars and above
   - 2 Stars and above
   - 1 Star and above
   - Unrated

### Filter by Tags

**AND Mode (default):**
- Select multiple tags
- Shows assets that have **ALL** selected tags

**OR Mode:**
- Click "OR" button
- Shows assets that have **ANY** selected tag

**Clear Filters:**
- Click "Clear" button
- Or deselect all tags

### Filter by File Type

1. Use the **file type dropdown** (right panel)
2. Options:
   - All Types
   - Images
   - Videos
   - Audio
   - Specific extensions (PNG, JPG, etc.)

### Combining Filters

All filters work together:
- Search + Rating + Tags + File Type
- Results show assets matching **ALL** criteria

---

## View Modes

### Grid View

**Best for:**
- Visual browsing
- Thumbnail previews
- Quick identification

**Features:**
- Adjustable thumbnail size
- File name display
- Rating stars
- Hover effects

### List View

**Best for:**
- Large libraries
- Detailed information
- Sorting by metadata

**Features:**
- Sortable columns
- Compact display
- All metadata visible
- Fast scrolling

---

## Settings and Database Management

### Opening Settings

1. Click **Settings** button in toolbar
2. Or use menu: File → Settings (not yet implemented)

### Database Operations

**Export Database:**
1. Open Settings
2. Click "Export Database"
3. Choose save location
4. Database is copied to selected location

**Import Database:**
1. Open Settings
2. Click "Import Database"
3. Select database file
4. **Warning**: Current database is replaced
5. Application restarts automatically

**Clear Database:**
1. Open Settings
2. Click "Clear Database"
3. Confirm action
4. **Warning**: All data is deleted permanently
5. Fresh database is created

### Backup Recommendations

- **Export database regularly** for backups
- Store backups in a safe location
- Consider version control for database files

---

## Keyboard Shortcuts

### Navigation
- **Left Arrow**: Previous asset (in preview)
- **Right Arrow**: Next asset (in preview)
- **Escape**: Close preview

### Selection (not yet implemented)
- **Ctrl+A**: Select all
- **Ctrl+D**: Deselect all

### Rating (not yet implemented)
- **0**: Clear rating
- **1-5**: Set rating

### View (not yet implemented)
- **Ctrl+G**: Grid view
- **Ctrl+L**: List view
- **Ctrl++**: Increase thumbnail size
- **Ctrl+-**: Decrease thumbnail size

---

## Tips and Best Practices

### Organization

1. **Create a folder structure** before importing
2. **Use descriptive folder names** (e.g., "2024-01-Project-Name")
3. **Tag assets immediately** after import
4. **Rate assets** to mark favorites or quality levels

### Performance

1. **Import in batches** for large libraries
2. **Wait for thumbnails** to generate before navigating
3. **Use filters** instead of scrolling through thousands of assets
4. **Close preview** when done to free memory

### Workflow

1. **Import → Tag → Rate → Filter**
2. Use **AND mode** for specific searches
3. Use **OR mode** for broad searches
4. **Export database** before major changes

### Tagging Strategy

1. **Use consistent naming** (e.g., "character", not "Character" or "char")
2. **Create hierarchical tags** (e.g., "project-name", "project-name-character")
3. **Avoid too many tags** per asset (5-10 is usually enough)
4. **Merge duplicate tags** regularly

---

## Troubleshooting

### Thumbnails Not Showing

**Problem**: Black squares instead of thumbnails

**Solutions:**
1. Wait for thumbnail generation to complete
2. Check if files still exist at original location
3. Restart application to reload cache
4. Check `data/thumbnails/` folder for cached files

### Import Not Working

**Problem**: Files not importing

**Solutions:**
1. Check file permissions
2. Ensure files are not locked by another application
3. Check available disk space
4. Try importing one file at a time

### Preview Not Opening

**Problem**: Double-click doesn't open preview

**Solutions:**
1. Check if file still exists
2. Try right-click → Preview
3. Check file format is supported
4. Restart application

### Database Errors

**Problem**: Database corruption or errors

**Solutions:**
1. Export database before attempting fixes
2. Try importing a backup database
3. Clear database and re-import assets
4. Check disk space and permissions

### Performance Issues

**Problem**: Application is slow

**Solutions:**
1. Reduce thumbnail size
2. Use filters to reduce visible assets
3. Close preview when not in use
4. Check available RAM
5. Restart application

### Missing Assets

**Problem**: Assets disappeared from library

**Solutions:**
1. Check if folder filter is active
2. Check if search filter is active
3. Check if rating filter is active
4. Clear all filters
5. Check if assets were moved to different folder

---

## Getting Help

### Support Resources

- **README.md**: Quick start guide
- **TECH.md**: Technical documentation
- **PERFORMANCE_OPTIMIZATIONS.md**: Performance details
- **GitHub Issues**: Report bugs and request features

### Reporting Bugs

When reporting bugs, please include:
1. **Steps to reproduce** the issue
2. **Expected behavior** vs actual behavior
3. **Screenshots** if applicable
4. **Log files** from `debug.log`
5. **System information** (Windows version, RAM, etc.)

---

## Conclusion

KAsset Manager is designed to make digital asset management simple and efficient. With its intuitive interface, powerful filtering, and professional features, you can organize thousands of assets with ease.

**Happy organizing!** 🎨📁

