# KAsset Manager - Developer Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Development Environment Setup](#development-environment-setup)
3. [Project Structure](#project-structure)
4. [Architecture Overview](#architecture-overview)
5. [Core Components](#core-components)
6. [Data Models](#data-models)
7. [Database Layer](#database-layer)
8. [UI Components](#ui-components)
9. [Build System](#build-system)
10. [Coding Standards](#coding-standards)
11. [Testing](#testing)
12. [Debugging](#debugging)
13. [Performance Optimization](#performance-optimization)
14. [Contributing](#contributing)

---

## Introduction

This guide is for developers who want to understand, modify, or contribute to KAsset Manager. It covers the architecture, code organization, and development practices.

### Technology Stack

- **Language**: C++20
- **UI Framework**: Qt 6.9.3 Widgets
- **Database**: SQLite 3 (via Qt SQL)
- **Build System**: CMake 3.21+ with Ninja or Visual Studio
- **Compiler**: MSVC 2022 (Windows)
- **Image Library**: OpenImageIO 3.0.9.1
- **Video**: Qt Multimedia with FFmpeg backend

---

## Development Environment Setup

### Prerequisites

1. **Visual Studio 2022** with C++ desktop development workload
2. **Qt 6.9.3** for MSVC 2022 64-bit
3. **CMake 3.21+**
4. **Ninja** (optional, but recommended)
5. **vcpkg** for OpenImageIO

### Installation Steps

```powershell
# 1. Install Qt 6.9.3
# Download from https://www.qt.io/download
# Install to C:\Qt\6.9.3\msvc2022_64

# 2. Install vcpkg
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat

# 3. Install OpenImageIO via vcpkg
.\vcpkg install openimageio:x64-windows

# 4. Clone the repository
git clone https://github.com/yourusername/KAssetManager.git
cd KAssetManager

# 5. Build the project
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -QtPrefix "C:\Qt\6.9.3\msvc2022_64" -Generator "Visual Studio 17 2022" -Package
```

### IDE Setup

**Visual Studio 2022:**
1. Open `native/qt6/build/vs2022/kassetmanagerqt.sln`
2. Set `kassetmanagerqt` as startup project
3. Build → Build Solution (Ctrl+Shift+B)
4. Debug → Start Debugging (F5)

**Qt Creator:**
1. Open `native/qt6/CMakeLists.txt`
2. Configure with Qt 6.9.3 kit
3. Build → Build Project
4. Run → Run (Ctrl+R)

---

## Project Structure

```
KAssetManager/
├── native/qt6/              # Qt 6 C++ application
│   ├── src/                 # Source files
│   │   ├── main.cpp         # Application entry point
│   │   ├── mainwindow.*     # Main window (UI + logic)
│   │   ├── db.*             # Database layer
│   │   ├── assets_model.*   # Assets data model
│   │   ├── assets_table_model.* # Table view adapter
│   │   ├── virtual_folders.* # Folder tree model
│   │   ├── tags_model.*     # Tags model
│   │   ├── importer.*       # File import logic
│   │   ├── live_preview_manager.* # Live preview streaming
│   │   ├── preview_overlay.* # Full-screen preview
│   │   ├── oiio_image_loader.* # OpenImageIO integration
│   │   └── settings_dialog.* # Settings UI
│   ├── CMakeLists.txt       # Build configuration
│   └── build/               # Build output (generated)
├── scripts/                 # Build and utility scripts
│   ├── build-windows.ps1    # Main build script
│   └── prepare-dependencies.ps1 # Dependency setup
├── dist/                    # Distribution output (generated)
│   └── portable/            # Portable application package
├── docs/                    # Documentation
├── README.md                # Quick start guide
├── USER_GUIDE.md            # User documentation
├── DEVELOPER_GUIDE.md       # This file
├── TECH.md                  # Technical overview
├── PERFORMANCE_OPTIMIZATIONS.md # Performance details
└── TASKS.md                 # Development roadmap
```

---

## Architecture Overview

### Design Pattern: Model-View-Delegate

KAsset Manager follows Qt's Model-View architecture:

```
┌─────────────────────────────────────────────────┐
│                  MainWindow                     │
│  (Coordinates views, handles events)            │
└─────────────────────────────────────────────────┘
           │              │              │
           ▼              ▼              ▼
    ┌──────────┐   ┌──────────┐   ┌──────────┐
    │ QTreeView│   │ QListView│   │ QTableView│
    │ (Folders)│   │ (Assets) │   │ (Assets)  │
    └──────────┘   └──────────┘   └──────────┘
           │              │              │
           ▼              ▼              ▼
    ┌──────────┐   ┌──────────┐   ┌──────────┐
    │  Folder  │   │  Assets  │   │  Assets  │
    │  Model   │   │  Model   │   │  Table   │
    └──────────┘   └──────────┘   └──────────┘
           │              │              │
           └──────────────┴──────────────┘
                         │
                         ▼
                   ┌──────────┐
                   │    DB    │
                   │ (SQLite) │
                   └──────────┘
```

### Key Principles

1. **Separation of Concerns**: UI, business logic, and data are separated
2. **Single Responsibility**: Each class has one clear purpose
3. **Qt Parent-Child Ownership**: Automatic memory management
4. **Signal-Slot Communication**: Loose coupling between components
5. **Lazy Loading**: Load data only when needed

---

## Core Components

### MainWindow (mainwindow.h/cpp)

**Purpose**: Main application window and coordinator

**Responsibilities:**
- Create and layout UI components
- Handle user interactions (clicks, drag-drop, context menus)
- Coordinate between models and views
- Manage application state

**Key Methods:**
```cpp
void setupUI();                    // Create UI components
void onFolderSelected(int folderId); // Handle folder selection
void onAssetDoubleClicked(const QModelIndex &index); // Open preview
void onImportFiles(const QStringList &files); // Import assets
void updateInfoPanel();            // Update info display
```

### DB (db.h/cpp)

**Purpose**: Database access layer (Singleton)

**Responsibilities:**
- Database connection management
- SQL query execution
- CRUD operations for all entities
- Database migrations

**Key Methods:**
```cpp
static DB& instance();             // Get singleton instance
bool open(const QString &path);    // Open database
bool migrate();                    // Run migrations
int createFolder(const QString &name, int parentId);
int createAsset(const QString &filePath, int folderId);
QList<int> getAssetTags(int assetId);
void setAssetRating(int assetId, int rating);
```

### AssetsModel (assets_model.h/cpp)

**Purpose**: Data model for asset grid/list views

**Responsibilities:**
- Load assets from database
- Provide data to views via Qt roles
- Handle filtering (search, tags, rating, file type)
- Notify views of data changes

**Key Methods:**
```cpp
void setFolderId(int folderId);    // Load assets for folder
void setSearchFilter(const QString &text);
void setRatingFilter(int minRating);
void setTagFilter(const QList<int> &tagIds, bool andMode);
void reload();                     // Refresh from database
```

### LivePreviewManager (live_preview_manager.h/cpp)
**Purpose**: Asynchronous, in-memory streaming of poster frames and scrub previews.

**Responsibilities**:
- Normalise preview requests (file path, target size, playback position).
- Dispatch decode jobs to background threads using FFmpeg (video) or OpenImageIO (image sequences).
- Maintain an LRU pixmap cache keyed by `(filePath, size, position)` with timed eviction.
- Emit `frameReady` / `frameFailed` signals for grid delegates, preview overlay, and sequence playback.
- Deduplicate warnings per asset to avoid log spam.

**Key API**:
```cpp
LivePreviewManager &mgr = LivePreviewManager::instance();
LivePreviewManager::FrameHandle handle = mgr.cachedFrame(path, targetSize, position);
if (handle.isValid()) {
    painter.drawPixmap(rect, handle.pixmap);
} else {
    mgr.requestFrame(path, targetSize, position);
}

connect(&mgr, &LivePreviewManager::frameReady, this, &MainWindow::onFrameReady);
connect(&mgr, &LivePreviewManager::frameFailed, this, &MainWindow::onFrameFailed);
```

**Notes**:
- No thumbnails are written to disk; everything stays in memory.
- FFmpeg decode features depend on the bundled `third_party/ffmpeg` build (see tooling scripts).
- Image sequence metadata is memoised so grouped entries scrub smoothly without blocking the UI.

### PreviewOverlay (preview_overlay.h/cpp)

**Purpose**: Full-screen preview for images, videos, and sequences

**Responsibilities:**
- Display images with zoom/pan
- Play videos with controls
- Play image sequences
- Handle keyboard navigation
- HDR/EXR color space selection

**Key Methods:**
```cpp
void showAsset(int assetId);       // Show asset in preview
void showImage(const QString &filePath);
void showVideo(const QString &filePath);
void showSequence(const QStringList &framePaths);
void navigateNext();               // Next asset
void navigatePrevious();           // Previous asset
```

---

## Data Models

### VirtualFolderTreeModel

**Base Class**: `QAbstractItemModel`

**Data Structure**: Hierarchical tree

**Roles:**
- `IdRole`: Folder ID (int)
- `NameRole`: Folder name (QString)
- `ParentIdRole`: Parent folder ID (int)

**Key Features:**
- Lazy loading for performance
- Drag-and-drop support
- Automatic refresh on changes

### AssetsModel

**Base Class**: `QAbstractListModel`

**Data Structure**: Flat list with filtering

**Roles:**
- `IdRole`: Asset ID (int)
- `FileNameRole`: File name (QString)
- `FilePathRole`: Full file path (QString)
- `FileSizeRole`: File size in bytes (qint64)
- `PreviewStateRole`: Live preview metadata (struct with cache state)
- `FileTypeRole`: File extension (QString)
- `LastModifiedRole`: Modification timestamp (QDateTime)
- `RatingRole`: Rating 0-5 (int)

**Filtering:**
- Search by file name (case-insensitive)
- Filter by rating (minimum rating)
- Filter by tags (AND/OR mode)
- Filter by file type (extension)

### AssetsTableModel

**Base Class**: `QAbstractTableModel`

**Purpose**: Adapter for QTableView (wraps AssetsModel)

**Columns:**
1. Name (sortable)
2. Type (sortable)
3. Size (sortable)
4. Date Modified (sortable)
5. Rating (sortable)

### TagsModel

**Base Class**: `QAbstractListModel`

**Data Structure**: Flat list

**Roles:**
- `IdRole`: Tag ID (int)
- `NameRole`: Tag name (QString)

---

## Database Layer

### Schema

```sql
-- Virtual folders
CREATE TABLE virtual_folders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    parent_id INTEGER NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP
);

-- Assets
CREATE TABLE assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path TEXT NOT NULL UNIQUE,
    file_name TEXT NOT NULL,
    virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id),
    file_size INTEGER NULL,
    mime_type TEXT NULL,
    rating INTEGER NULL,
    created_at TEXT DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT DEFAULT CURRENT_TIMESTAMP,
    is_sequence INTEGER DEFAULT 0,
    sequence_pattern TEXT NULL,
    sequence_start_frame INTEGER NULL,
    sequence_end_frame INTEGER NULL,
    sequence_frame_count INTEGER NULL
);

-- Tags
CREATE TABLE tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE
);

-- Asset-Tag relationship
CREATE TABLE asset_tags (
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    PRIMARY KEY (asset_id, tag_id)
);
```

### Indexes

```sql
-- Performance indexes
CREATE INDEX idx_virtual_folders_parent_name ON virtual_folders(parent_id, name);
CREATE INDEX idx_assets_folder ON assets(virtual_folder_id);
CREATE INDEX idx_assets_file_name ON assets(file_name);
CREATE INDEX idx_assets_rating ON assets(rating);
CREATE INDEX idx_assets_updated_at ON assets(updated_at);
CREATE INDEX idx_asset_tags_tag_id ON asset_tags(tag_id);
CREATE INDEX idx_asset_tags_asset_id ON asset_tags(asset_id);
```

### Database Operations

**Best Practices:**
1. Use prepared statements for repeated queries
2. Use transactions for batch operations
3. Check return values and handle errors
4. Use indexes for frequently queried columns
5. Avoid N+1 query problems

**Example:**
```cpp
// Good: Prepared statement with transaction
QSqlDatabase db = DB::instance().database();
db.transaction();

QSqlQuery q(db);
q.prepare("INSERT INTO assets (file_path, file_name, virtual_folder_id) VALUES (?, ?, ?)");

for (const QString &filePath : filePaths) {
    q.addBindValue(filePath);
    q.addBindValue(QFileInfo(filePath).fileName());
    q.addBindValue(folderId);
    if (!q.exec()) {
        qWarning() << "Failed to insert asset:" << q.lastError();
        db.rollback();
        return false;
    }
}

db.commit();
```

---

## UI Components

### Custom Delegates

**AssetItemDelegate** (in mainwindow.cpp)

**Purpose**: Custom rendering for asset grid items

**Features:**
- Live preview display via `LivePreviewManager`
- File name text
- Rating stars
- Hover effects
- Pixmap caching

**Paint Method:**
```cpp
void paint(QPainter *painter, const QStyleOptionViewItem &option,
           const QModelIndex &index) const override
{
    // 1. Query LivePreviewManager cache or request decode
    // 2. Draw card background
    // 3. Draw the pixmap inside the inset preview rect
    // 4. Draw file name (bottom, wrapped)
    // 5. Draw rating stars (if rated)
    // 6. Draw selection highlight
}
```

### Preview Overlay

**Components:**
- `QGraphicsView` + `QGraphicsScene` for images
- `QMediaPlayer` + `QVideoWidget` for videos
- Custom controls for playback
- Keyboard event handling

**Image Zoom/Pan:**
```cpp
// Zoom with mouse wheel
void wheelEvent(QWheelEvent *event) {
    double scaleFactor = 1.15;
    if (event->angleDelta().y() > 0) {
        imageView->scale(scaleFactor, scaleFactor);
    } else {
        imageView->scale(1.0 / scaleFactor, 1.0 / scaleFactor);
    }
}

// Pan with mouse drag
void mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    }
}
```

---

## Build System

### CMake Configuration

**Key Settings:**
```cmake
cmake_minimum_required(VERSION 3.21)
project(KAssetManagerQt VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia Sql)
find_package(OpenImageIO CONFIG REQUIRED)

add_executable(kassetmanagerqt
    src/main.cpp
    src/mainwindow.cpp
    # ... other sources
)

target_link_libraries(kassetmanagerqt PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::Sql
    OpenImageIO::OpenImageIO
)
```

### Build Script

**build-windows.ps1** parameters:
- `-QtPrefix`: Path to Qt installation
- `-Generator`: CMake generator (Ninja or "Visual Studio 17 2022")
- `-Package`: Create portable package
- `-Clean`: Clean build directory first

**Usage:**
```powershell
# Development build (fast)
.\scripts\build-windows.ps1 -Generator Ninja

# Release build with package
.\scripts\build-windows.ps1 -Generator Ninja -Package

# Visual Studio solution
.\scripts\build-windows.ps1 -Generator "Visual Studio 17 2022"
```

---

## Coding Standards

### Naming Conventions

**Classes**: PascalCase
```cpp
class LivePreviewManager { };
class AssetsModel { };
```

**Functions/Methods**: camelCase
```cpp
void loadAssets();
int getAssetId() const;
```

**Variables**: camelCase
```cpp
int assetId = 0;
QString filePath;
```

**Member Variables**: m_ prefix (optional)
```cpp
class MyClass {
private:
    int m_count;
    QString m_name;
};
```

**Constants**: UPPER_SNAKE_CASE or kPascalCase
```cpp
constexpr int kPreviewInset = 8;
constexpr int kDefaultRating = 0;
```

### Code Style

**Indentation**: 4 spaces (no tabs)

**Braces**: Opening brace on same line
```cpp
if (condition) {
    // code
} else {
    // code
}
```

**Pointers/References**: Attach to type
```cpp
QString* ptr;      // Good
QString *ptr;      // Avoid

const QString& ref; // Good
const QString &ref; // Avoid
```

**Include Order:**
1. Corresponding header
2. Qt headers
3. Standard library headers
4. Third-party headers

```cpp
#include "mainwindow.h"

#include <QApplication>
#include <QMessageBox>

#include <iostream>
#include <vector>

#include <OpenImageIO/imageio.h>
```

### Memory Management

**Use Qt parent-child ownership:**
```cpp
// Good: Parent owns child
QWidget* widget = new QWidget(parent);

// Good: Smart pointer for non-QObject
auto data = std::make_unique<MyData>();

// Avoid: Manual new/delete
MyClass* obj = new MyClass();
delete obj;
```

**Use RAII:**
```cpp
// Good: Automatic cleanup
{
    QFile file("data.txt");
    if (file.open(QIODevice::ReadOnly)) {
        // Use file
    } // Automatically closed
}
```

### Error Handling

**Check return values:**
```cpp
if (!db.open("kasset.db")) {
    qCritical() << "Failed to open database";
    return false;
}
```

**Use qWarning/qCritical for logging:**
```cpp
qDebug() << "Debug info";
qWarning() << "Warning message";
qCritical() << "Critical error";
```

**Display user-friendly errors:**
```cpp
QMessageBox::critical(this, "Error",
    "Failed to import files. Please check file permissions.");
```

---

## Testing

### Manual Testing Checklist

**Import:**
- [ ] Import single file
- [ ] Import multiple files
- [ ] Import folder
- [ ] Import image sequence
- [ ] Cancel import (not implemented)

**Navigation:**
- [ ] Select folder
- [ ] Create folder
- [ ] Rename folder
- [ ] Delete folder
- [ ] Move assets between folders

- [ ] File Manager: Double-clicking a folder in the right pane expands/selects the same folder in the left tree; folders-first sorting keeps folders above files for all sort columns and orders

**Selection:**
- [ ] Single select
- [ ] Ctrl+Click multi-select
- [ ] Shift+Click range select

**Tagging:**
- [ ] Create tag
- [ ] Assign tag
- [ ] Remove tag
- [ ] Rename tag
- [ ] Delete tag
- [ ] Merge tags

**Rating:**
- [ ] Set rating
- [ ] Clear rating
- [ ] Filter by rating

**Preview:**
- [ ] Preview image
- [ ] Preview video
- [ ] Preview sequence
- [ ] Navigate with arrows
- [ ] Zoom/pan image
- [ ] Play/pause video

- [ ] On closing full-size preview, focus/selection return to the item you opened from (Asset Manager or File Manager); arrow keys work immediately

**Performance:**
- [ ] Import 100+ files
- [ ] Navigate large folders
- [ ] Scroll through assets
- [ ] Filter large libraries

### Unit Testing (Future)

**Recommended framework**: Qt Test

**Test coverage goals:**
- Database operations: 80%+
- Data models: 70%+
- Business logic: 60%+

---

## Debugging

### Debug Logging

**Enable debug output:**
```cpp
qDebug() << "[Component] Message:" << variable;
```

**Log file**: `debug.log` in application directory

**View logs:**
```powershell
Get-Content debug.log -Tail 50 -Wait
```

### Common Issues

**Crash on startup:**
- Check Qt DLLs are present
- Check database file permissions
- Check debug.log for errors

**Live preview not showing:**
- Check `debug.log` for `[LivePreview]` errors (permission denied, codec missing).
- Verify the media file can be opened via the File Manager preview panel.
- Ensure the bundled FFmpeg DLLs were refreshed with `scripts/fetch-ffmpeg.ps1`.

**Database errors:**
- Check database file is not locked
- Check SQL syntax in queries
- Enable SQL logging: `qDebug() << query.lastQuery();`

### Debugging Tools

**Visual Studio Debugger:**
- Set breakpoints (F9)
- Step over (F10)
- Step into (F11)
- Watch variables
- Call stack

**Qt Creator Debugger:**
- Similar to Visual Studio
- Better Qt object inspection
- QML profiler (not used in this project)

---

## Performance Optimization

See [PERFORMANCE_OPTIMIZATIONS.md](PERFORMANCE_OPTIMIZATIONS.md) for detailed information.

**Key optimizations:**
1. Background FFmpeg/OpenImageIO decode jobs managed by LivePreviewManager
2. Database indexes on frequently queried columns
3. In-memory pixmap cache (~512MB default) with LRU eviction
4. Lazy loading for asset grid
5. Prepared statements for database queries

**Profiling:**
```cpp
QElapsedTimer timer;
timer.start();
// ... code to profile
qDebug() << "Operation took" << timer.elapsed() << "ms";
```

---

## Contributing

### Getting Started

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

### Pull Request Guidelines

**Title**: Clear, descriptive summary

**Description**: Include:
- What changed
- Why it changed
- How to test
- Screenshots (if UI change)

**Code Quality:**
- Follow coding standards
- Add comments for complex logic
- Update documentation
- No compiler warnings

### Code Review Process

1. Automated checks (build, tests)
2. Code review by maintainer
3. Address feedback
4. Approval and merge

---

## Conclusion

This guide provides the foundation for developing KAsset Manager. For specific technical details, refer to:

- **TECH.md**: Technology stack and architecture
- **PERFORMANCE_OPTIMIZATIONS.md**: Performance details
- **USER_GUIDE.md**: User-facing features

**Happy coding!** 💻🚀

