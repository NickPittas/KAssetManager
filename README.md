# KAsset Manager

**Professional asset management software for Windows** - Organize, tag, rate, and manage digital assets including images (PNG, JPG, TIF, EXR, IFF, PSD), videos (MOV, MP4, AVI), and audio (MP3) files.

## Overview

KAsset Manager is a native Windows desktop application built with Qt 6 that provides a professional-grade solution for managing digital media assets. Whether you're a VFX artist, photographer, video editor, or content creator, KAsset Manager helps you organize, find, and preview your files efficiently.

### Key Features

#### 🤖 **Intelligent Features (v0.3.0)**
- **Everything Search Integration** - Ultra-fast disk-wide search with bulk import
- **Database Health Agent** - Automated health checks and maintenance
- **Bulk Rename Intelligence** - Pattern-based renaming with preview and rollback
- **Sequence Intelligence** - Automatic gap detection and version tracking
- **Context Preserver** - Per-folder UI state persistence

See [INTELLIGENT_FEATURES.md](INTELLIGENT_FEATURES.md) for complete installation and usage instructions.

#### 🗂️ **Organization**
- **Virtual Folder System** - Organize assets without moving files on disk
- **Hierarchical Structure** - Create nested folders for complex projects
- **Drag-and-Drop Import** - Import files and folders with a simple drag
- **Batch Operations** - Move, tag, and rate multiple assets at once

- **Add to Library (Explorer-style)** - From File Manager, add files or entire folders; when folders are selected, their subfolder hierarchy is preserved and recreated in the Asset Manager

#### 🏷️ **Tagging & Rating**
- **Multi-Tag Support** - Assign multiple tags to each asset
- **Tag Management** - Create, rename, delete, and merge tags
- **5-Star Rating System** - Rate assets for quality or importance
- **Smart Filtering** - Filter by tags (AND/OR mode), rating, and file type

#### 🔍 **Search & Filter**
- **Real-time Search** - Find assets by name instantly
- **Advanced Filters** - Combine search, tags, rating, and file type
- **Sortable Views** - Sort by name, type, size, date, or rating
- **Grid & List Views** - Switch between thumbnail grid and detailed list

- **Folders-first Sorting** - In File Manager (grid and list), folders are always listed before files regardless of sort column or order

#### 👁️ **Preview & Playback**
- **Full-Screen Preview** - View images, videos, and sequences
- **Image Zoom & Pan** - Inspect images in detail
- **Video Playback** - Play videos with timeline and volume controls
- **Image Sequences** - Automatic detection and playback at 24fps
- **HDR/EXR Support** - Color space selection (Linear, sRGB, Rec.709)
- **Hover Scrubbing** - Hold Ctrl over grid cards to scrub videos and image sequences

- **Focus Restoration** - When closing full-size preview, selection and keyboard focus return to the previously selected item so you can continue navigating with arrow keys instantly

#### 🚀 **Performance**
- **Live Preview Streaming** - FFmpeg/OpenImageIO decode with in-memory caching
- **Smart Caching** - LRU pixmap cache (~512MB) keeps recent frames warm
- **Database Indexes** - Optimized queries for large libraries
- **Lazy Loading** - Decode only when cards enter the viewport

#### 📊 **Professional Formats**
- **Images**: PNG, JPG, JPEG, BMP, GIF, TIFF, TIF, EXR, HDR, PSD, IFF, RAW
- **Videos**: MOV, MP4, AVI, MP5, MKV, WMV (via FFmpeg)
- **Audio**: MP3, WAV, OGG, FLAC
- **Sequences**: Automatic detection of numbered image sequences

## Quick Start

### Build and Run

```powershell
# Build and package (creates dist/portable/)
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package

# Run the application
.\dist\portable\bin\kassetmanagerqt.exe
```

### Using the Application

1. **Import Assets** - Drag and drop files or folders onto the main window
   - Or select files/folders in File Manager and click "Add to Library" (folders preserve their hierarchy in the Asset Manager)

2. **Navigate Folders** - Click folders in the left tree to view their contents
3. **Select Assets** - Click to select, Ctrl+Click to multi-select, Shift+Click for range
4. **Tag Assets** - Right-click assets and choose "Assign Tag"
5. **Rate Assets** - Right-click and choose "Set Rating"
6. **Filter** - Use the filters panel on the right to search and filter assets
7. **Preview** - Double-click an asset to open preview mode

## Screenshots

*Coming soon - Screenshots will be added in a future update*

## Documentation

### For Users
- **[INTELLIGENT_FEATURES.md](INTELLIGENT_FEATURES.md)** - ⭐ **NEW!** Installation and usage guide for intelligent features
- **[USER_GUIDE.md](USER_GUIDE.md)** - Complete user guide with tutorials and workflows
- **[INSTALL.md](INSTALL.md)** - Installation and build instructions

### For Developers
- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)** - Architecture, code structure, and contribution guidelines
- **[NEW_FEATURES.md](NEW_FEATURES.md)** - Intelligent features roadmap and implementation status
- **[API_REFERENCE.md](API_REFERENCE.md)** - Complete API documentation for all classes
- **[TECH.md](TECH.md)** - Technology stack and design decisions
- **[PERFORMANCE_OPTIMIZATIONS.md](PERFORMANCE_OPTIMIZATIONS.md)** - Performance optimization details
- **[TASKS.md](TASKS.md)** - Development tasks and roadmap

## Project Structure

```
KAssetManager/
├── native/qt6/          # Qt 6 C++ application
│   ├── src/             # C++ source files
│   │   ├── mainwindow.* # Main application window
│   │   ├── db.*         # SQLite database layer
│   │   ├── assets_model.* # Assets data model
│   │   ├── virtual_folders.* # Folder tree model
│   │   ├── tags_model.* # Tags model
│   │   ├── importer.*   # File import logic
│   │   └── live_preview_manager.* # Live preview streaming
│   ├── CMakeLists.txt   # Build configuration
│   └── build/           # Build output (generated)
├── scripts/             # Build scripts
│   └── build-windows.ps1 # Windows build script
├── dist/                # Distribution output (generated)
│   └── portable/        # Portable application package
└── docs/                # Documentation
```

## Database Schema

SQLite database stored in persistent user data location (see [INSTALL.md](INSTALL.md) for exact path):

- **virtual_folders** - Folder hierarchy
- **assets** - Asset metadata (file path, size, type, rating, etc.)
- **tags** - Tag definitions
- **asset_tags** - Many-to-many relationship between assets and tags
- **asset_versions** - Version history for assets
- **project_folders** - Watched project folders

## Troubleshooting

### Common Issues

**Live preview not showing:**
- Give the decoder a moment to cache the first frame (large EXR/ProRes files can take a second).
- Check `debug.log` for `[LivePreview]` warnings about codecs or permissions.
- Make sure the bundled FFmpeg DLLs were refreshed with `scripts/fetch-ffmpeg.ps1`.

**Import not working:**
- Verify file permissions (files must be readable)
- Ensure files are not locked by another application
- Check available disk space for the database and cached previews

**Preview not opening:**
- Verify the file format is supported
- Check that the file still exists at the original path
- Try right-click → Preview instead of double-click

**Performance issues:**
- Reduce thumbnail size using the slider
- Use filters to reduce the number of visible assets
- Close preview when not in use to free memory
- Restart the application if it becomes sluggish

**Database errors:**
- Export your database as a backup (Settings → Export Database)
- Try importing a backup if corruption occurs
- As a last resort, clear the database and re-import assets

### Getting Help

- Check the **[USER_GUIDE.md](USER_GUIDE.md)** for detailed instructions
- Review **[TECH.md](TECH.md)** for technical information
- Report bugs on GitHub Issues (include steps to reproduce)

## System Requirements

- **OS**: Windows 10/11 (64-bit)
- **RAM**: 4GB minimum, 8GB+ recommended
- **Disk**: 500MB for application plus space for cached previews
- **Display**: 1920x1080 or higher recommended

## License

Proprietary - All rights reserved.
