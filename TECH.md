# Technology Stack

## Overview

KAsset Manager is built with **Qt 6 Widgets** (C++20) for native Windows desktop performance and reliability.

## Core Technologies

### UI Framework

- **Qt 6.9.3 Widgets** - Native desktop UI framework
  - QMainWindow - Application window
  - QTreeView - Folder tree navigation
  - QListView - Asset grid with icon mode
  - QSplitter - Resizable panels
  - Custom QStyledItemDelegate - Live preview rendering
  - Native multi-select (Ctrl+Click, Shift+Click)
  - Native drag-and-drop support
  - Native context menus

### Programming Language

- **C++20** - Modern C++ with:
  - Standard library containers
  - Smart pointers
  - Lambda expressions
  - Range-based for loops
  - Structured bindings

### Database

- **SQLite 3** - Embedded SQL database
  - Qt SQL module (QtSql)
  - QSqlDatabase, QSqlQuery
  - ACID transactions
  - Full-text search support

### Build System

- **CMake 3.21+** - Cross-platform build configuration
- **Ninja** - Fast build system (recommended)
- **MinGW 13.1.0** - GCC compiler for Windows

### Media Support

- **Qt Multimedia** - Audio/video playback
  - FFmpeg backend (bundled Qt 6.9.3 + custom full-shared build in `third_party/ffmpeg`)
  - Hardware-accelerated decoding
  - Format support: MP4, MOV, AVI, MP3, WAV

### Image Support

- **Qt Image Plugins** - Built-in image loading
  - PNG, JPEG, BMP, GIF, TIFF
  - Future: OpenImageIO for EXR, PSD, IFF

## Architecture

### Application Structure

```
MainWindow (QMainWindow)
├── Left Panel: Folder Tree (QTreeView)
│   └── VirtualFolderTreeModel (QAbstractItemModel)
├── Center Panel: Asset Grid (QListView)
│   ├── AssetsModel (QAbstractListModel)
│   └── AssetItemDelegate (QStyledItemDelegate)
└── Right Panel: Filters + Info (QWidget)
    ├── Filters Panel
    │   ├── Search (QLineEdit)
    │   ├── Rating Filter (QComboBox)
    │   └── Tags List (QListView + TagsModel)
    └── Info Panel
        └── Asset Metadata Labels
```

### Data Models

#### VirtualFolderTreeModel
- Hierarchical folder structure
- Lazy loading for performance
- Drag-and-drop support for moving assets
- Roles: IdRole, NameRole, ParentIdRole

#### AssetsModel
- Flat list of assets in current folder
- Live preview caching (in-memory via LivePreviewManager)
- Filtering support (search, tags, rating)
- Roles: IdRole, FileNameRole, FilePathRole, FileSizeRole, PreviewStateRole, FileTypeRole, LastModifiedRole, RatingRole

#### TagsModel
- List of available tags
- Multi-selection support
- Roles: IdRole, NameRole

### Database Schema

```sql
-- Virtual folder hierarchy
CREATE TABLE virtual_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    parent_id INTEGER,
    FOREIGN KEY (parent_id) REFERENCES virtual_folders(id) ON DELETE CASCADE
);

-- Asset metadata
CREATE TABLE assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_name TEXT NOT NULL,
    file_path TEXT NOT NULL UNIQUE,
    file_size INTEGER,
    file_type TEXT,
    last_modified INTEGER,
    rating INTEGER DEFAULT 0,
    folder_id INTEGER NOT NULL,
    FOREIGN KEY (folder_id) REFERENCES virtual_folders(id) ON DELETE CASCADE
);

-- Tags
CREATE TABLE tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

-- Asset-Tag relationship (many-to-many)
CREATE TABLE asset_tags (
    asset_id INTEGER NOT NULL,
    tag_id INTEGER NOT NULL,
    PRIMARY KEY (asset_id, tag_id),
    FOREIGN KEY (asset_id) REFERENCES assets(id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
);
```

### Database Indexes

The following indexes are created to optimize query performance:

```sql
-- Explicit index for file_path lookups (UNIQUE constraint creates implicit index, but explicit is clearer)
CREATE UNIQUE INDEX idx_assets_file_path ON assets(file_path);

-- Folder hierarchy lookups
CREATE UNIQUE INDEX idx_virtual_folders_parent_name ON virtual_folders(parent_id, name);

-- Asset queries by folder
CREATE INDEX idx_assets_folder ON assets(virtual_folder_id);

-- Asset queries by file name
CREATE INDEX idx_assets_file_name ON assets(file_name);

-- Asset queries by rating
CREATE INDEX idx_assets_rating ON assets(rating);

-- Asset queries by update time
CREATE INDEX idx_assets_updated_at ON assets(updated_at);

-- Asset-tag relationship queries
CREATE INDEX idx_asset_tags_tag_id ON asset_tags(tag_id);
CREATE INDEX idx_asset_tags_asset_id ON asset_tags(asset_id);

-- Asset version queries
CREATE INDEX idx_asset_versions_asset_id ON asset_versions(asset_id);
```

**Index Strategy:**
- `idx_assets_file_path`: Explicit UNIQUE index for file existence checks and asset lookups by path
- `idx_assets_folder`: Enables fast folder navigation and asset listing
- `idx_assets_file_name`: Supports sorting and filtering by file name
- `idx_assets_rating`: Enables fast rating-based filtering
- `idx_assets_updated_at`: Supports time-based queries and sorting
- Composite indexes on foreign keys for efficient joins

### Key Components

#### MainWindow (mainwindow.h/cpp)
- Main application window
- UI setup and layout
- Event handling (drag-drop, selection, context menus)
- Coordinates between models and views

#### DB (db.h/cpp)
- Singleton database manager
- SQL query execution
- CRUD operations for folders, assets, tags
- Database initialization and migrations

#### LivePreviewManager (live_preview_manager.h/cpp)
- Manages in-memory poster frames and hover scrubbing
- Normalises requests (path, size, position)
- Dispatches FFmpeg/OpenImageIO decode jobs on background threads
- Emits `frameReady` / `frameFailed` signals consumed by grid delegates and the preview overlay
- Maintains an LRU pixmap cache (~512 MB default)
- Reconstructs image sequence frame lists for grouped entries

### PreviewOverlay (preview_overlay.h/cpp)
- Full-screen preview window (images, videos, sequences)
- Live scrubbing, playback controls, metadata display
- Reuses LivePreviewManager frames for consistency

### Importer (importer.h/cpp)
- Background file import
- Metadata extraction and checksum validation
- Progress reporting
- File type detection

#### AssetsModel (assets_model.h/cpp)
- Asset data model for QListView
- Filtering and sorting
- Live preview lookup via LivePreviewManager
- Data refresh on changes

#### VirtualFolderTreeModel (virtual_folders.h/cpp)
- Folder tree data model for QTreeView
- Hierarchical data structure
- Lazy loading
- Drag-and-drop support

## Design Decisions

### Why Qt Widgets Instead of QML?

**Previous attempt with QML failed due to:**
- Unreliable mouse event handling
- Modifier keys (Ctrl/Shift) not working
- Complex event propagation issues
- 2 days of debugging basic interactions

**Qt Widgets advantages:**
- Mature, battle-tested (25+ years)
- Native Windows look and feel
- Reliable event handling
- Multi-select works out of the box
- Better performance for desktop apps
- Extensive documentation and examples

### Why SQLite?

- Embedded (no server required)
- Zero configuration
- ACID compliant
- Fast for read-heavy workloads
- Portable (single file database)
- Excellent Qt integration

### Why C++20?

- Native performance
- Direct Qt API access
- Modern language features
- Strong typing
- Memory safety with smart pointers

## Performance Considerations

### Live Preview Streaming

- LivePreviewManager decodes frames on demand and caches pixmaps in memory
- Grid delegates clamp rendering to inset card bounds
- Background jobs use FFmpeg (video) and OpenImageIO (image sequences)
- Cache eviction is LRU-based and tunable via code constants

### Database Optimization

- Indexes on frequently queried columns
- Prepared statements for repeated queries
- Batch operations for imports
- Connection pooling

### UI Responsiveness

- Asynchronous operations (import, live preview decoding)
- Progress reporting for long operations
- Lazy loading in tree and list views
- Efficient repainting with custom delegates

## Future Enhancements

### Planned Features

1. **Advanced Image Format Support**
   - OpenImageIO integration for EXR, PSD, IFF
   - Layer support for PSD files
   - HDR image display

2. **Video Playback**
   - Full-screen video player
   - Frame-by-frame navigation
   - Timeline scrubbing
   - Multiple codec support

3. **Metadata Editing**
   - EXIF/XMP/IPTC editing
   - Batch metadata operations
   - Custom metadata fields

4. **Search and Filtering**
   - Full-text search
   - Advanced query builder
   - Saved searches
   - Smart folders

5. **Export and Sharing**
   - Batch export with presets
   - Cloud storage integration
   - Collection sharing

### Technical Debt

- Add comprehensive unit tests
- Implement undo/redo system
- Add logging framework
- Improve error handling
- Add crash reporting

## Development Guidelines

### Code Style

- Follow Qt coding conventions
- Use `camelCase` for variables and functions
- Use `PascalCase` for classes
- Prefix member variables with `m_` (optional)
- Use `const` and `constexpr` where possible

### Memory Management

- Use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Qt parent-child ownership for QObjects
- RAII for resource management
- Avoid manual `new`/`delete`

### Error Handling

- Check return values from Qt functions
- Use `qWarning()`, `qCritical()` for logging
- Display user-friendly error messages
- Graceful degradation on errors

### Testing

- Unit tests for business logic
- Integration tests for database operations
- Manual testing for UI interactions
- Performance testing for large datasets

## Dependencies

### Runtime Dependencies

- Qt 6.9.3 (Widgets, Multimedia, SQL)
- MinGW runtime DLLs
- FFmpeg (bundled with Qt)
- SQLite (bundled with Qt)

### Build Dependencies

- CMake 3.21+
- Ninja or Visual Studio
- MinGW 13.1.0 (GCC)
- Qt 6.9.3 SDK

## Platform Support

### Current

- Windows 10/11 (64-bit)

### Future

- macOS (Qt Widgets is cross-platform)
- Linux (Qt Widgets is cross-platform)

## Version History

### v1.0.5 (Current)

- Database persistence fix (survives app updates)
- Database stored in `AppData/Roaming/KAsset/KAsset Manager Qt/`
- Automatic migration from old location
- Crash dump location moved to persistent directory

### v0.2.0

- File Manager improvements
- Folder hierarchy preservation
- Folders-first sorting
- Preview overlay focus improvements

### v0.1.0

- Initial Qt Widgets implementation
- Folder tree navigation
- Asset grid view with live preview cards
- Multi-select support
- Drag-and-drop import
- Tagging system
- Rating system
- Filters panel
- Info panel
- Context menus

### Previous (Deprecated)

- v0.0.x - QML implementation (abandoned due to reliability issues)

