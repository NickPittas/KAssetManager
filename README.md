# KAsset Manager

**Professional asset management software for Windows** - Organize, tag, rate, and manage digital assets including images (PNG, JPG, TIF, EXR, IFF, PSD), videos (MOV, MP4, AVI), and audio (MP3) files.

## Overview

KAsset Manager provides a Windows Explorer-like interface for managing digital assets with:

- **Folder Tree Navigation** - Organize assets in virtual folders
- **Asset Grid View** - Thumbnail previews with file information
- **Multi-Select** - Ctrl+Click (toggle), Shift+Click (range selection)
- **Drag-and-Drop Import** - Drop files or folders to import
- **Tagging System** - Assign multiple tags to assets
- **Rating System** - 5-star rating for assets
- **Filters** - Search, filter by tags, rating, file type
- **Metadata Display** - View file size, type, modification date
- **Context Menus** - Right-click for quick actions
- **Preview** - View assets in full-screen preview mode

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
2. **Navigate Folders** - Click folders in the left tree to view their contents
3. **Select Assets** - Click to select, Ctrl+Click to multi-select, Shift+Click for range
4. **Tag Assets** - Right-click assets and choose "Assign Tag"
5. **Rate Assets** - Right-click and choose "Set Rating"
6. **Filter** - Use the filters panel on the right to search and filter assets
7. **Preview** - Double-click an asset to open preview mode

## Documentation

- **[INSTALL.md](INSTALL.md)** - Installation and build instructions
- **[TECH.md](TECH.md)** - Technology stack and architecture
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
│   │   └── thumbnail_generator.* # Thumbnail generation
│   ├── CMakeLists.txt   # Build configuration
│   └── build/           # Build output (generated)
├── scripts/             # Build scripts
│   └── build-windows.ps1 # Windows build script
├── dist/                # Distribution output (generated)
│   └── portable/        # Portable application package
└── docs/                # Documentation
```

## Database Schema

SQLite database stored in `data/kasset.db`:

- **virtual_folders** - Folder hierarchy
- **assets** - Asset metadata (file path, size, type, rating, etc.)
- **tags** - Tag definitions
- **asset_tags** - Many-to-many relationship between assets and tags

## License

Proprietary - All rights reserved.
