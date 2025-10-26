# KAsset Manager - API Reference

## Table of Contents

1. [DB Class](#db-class)
2. [AssetsModel Class](#assetsmodel-class)
3. [VirtualFolderTreeModel Class](#virtualfoldertreemodel-class)
4. [TagsModel Class](#tagsmodel-class)
5. [ThumbnailGenerator Class](#thumbnailgenerator-class)
6. [PreviewOverlay Class](#previewoverlay-class)
7. [OIIOImageLoader Class](#oiioimageloader-class)
8. [AssetsTableModel Class](#assetstablemodel-class)

---

## DB Class

**Header**: `native/qt6/src/db.h`

**Purpose**: Singleton database access layer for SQLite operations

**Base Class**: `QObject`

### Static Methods

#### `static DB& instance()`
Get the singleton instance of the database manager.

**Returns**: Reference to the DB singleton

**Example**:
```cpp
DB& db = DB::instance();
```

### Initialization

#### `bool init(const QString& dbFilePath)`
Initialize the database connection and run migrations.

**Parameters**:
- `dbFilePath`: Full path to the SQLite database file

**Returns**: `true` on success, `false` on failure

**Example**:
```cpp
if (!DB::instance().init("data/kasset.db")) {
    qCritical() << "Failed to initialize database";
}
```

#### `QSqlDatabase database() const`
Get the underlying QSqlDatabase object for direct queries.

**Returns**: QSqlDatabase instance

### Folder Operations

#### `int ensureRootFolder()`
Ensure the root folder exists, creating it if necessary.

**Returns**: Root folder ID, or 0 on failure

#### `int createFolder(const QString& name, int parentId)`
Create a new virtual folder.

**Parameters**:
- `name`: Folder name
- `parentId`: Parent folder ID (0 or negative for root)

**Returns**: New folder ID, or 0 on failure

**Emits**: `foldersChanged()`

#### `bool renameFolder(int id, const QString& name)`
Rename an existing folder.

**Parameters**:
- `id`: Folder ID
- `name`: New folder name

**Returns**: `true` on success, `false` on failure

**Emits**: `foldersChanged()`

#### `bool deleteFolder(int id)`
Delete a folder (assets are moved to root, not deleted).

**Parameters**:
- `id`: Folder ID to delete

**Returns**: `true` on success, `false` on failure

**Emits**: `foldersChanged()`

#### `bool moveFolder(int id, int newParentId)`
Move a folder to a new parent.

**Parameters**:
- `id`: Folder ID to move
- `newParentId`: New parent folder ID

**Returns**: `true` on success, `false` on failure

**Emits**: `foldersChanged()`

### Asset Operations

#### `int upsertAsset(const QString& filePath)`
Insert or update an asset by file path.

**Parameters**:
- `filePath`: Full path to the asset file

**Returns**: Asset ID, or 0 on failure

**Emits**: `assetsChanged(folderId)`

#### `int upsertSequence(const QString& sequencePattern, int startFrame, int endFrame, int frameCount, const QString& firstFramePath)`
Insert or update an image sequence.

**Parameters**:
- `sequencePattern`: Pattern string (e.g., "render.####.exr")
- `startFrame`: First frame number
- `endFrame`: Last frame number
- `frameCount`: Total number of frames
- `firstFramePath`: Path to the first frame

**Returns**: Asset ID, or 0 on failure

**Emits**: `assetsChanged(folderId)`

#### `bool setAssetFolder(int assetId, int folderId)`
Move an asset to a different folder.

**Parameters**:
- `assetId`: Asset ID
- `folderId`: Target folder ID

**Returns**: `true` on success, `false` on failure

**Emits**: `assetsChanged(oldFolderId)`, `assetsChanged(newFolderId)`

#### `bool removeAssets(const QList<int>& assetIds)`
Delete multiple assets from the database.

**Parameters**:
- `assetIds`: List of asset IDs to delete

**Returns**: `true` on success, `false` on failure

**Emits**: `assetsChanged(folderId)` for each affected folder

#### `bool setAssetsRating(const QList<int>& assetIds, int rating)`
Set rating for multiple assets.

**Parameters**:
- `assetIds`: List of asset IDs
- `rating`: Rating value (0-5, or -1 to clear)

**Returns**: `true` on success, `false` on failure

**Emits**: `assetsChanged(folderId)` for each affected folder

#### `QList<int> getAssetIdsInFolder(int folderId, bool recursive = true) const`
Get all asset IDs in a folder.

**Parameters**:
- `folderId`: Folder ID
- `recursive`: Include subfolders (default: true)

**Returns**: List of asset IDs

#### `QString getAssetFilePath(int assetId) const`
Get the file path for an asset.

**Parameters**:
- `assetId`: Asset ID

**Returns**: File path, or empty string if not found

### Tag Operations

#### `int createTag(const QString& name)`
Create a new tag.

**Parameters**:
- `name`: Tag name (must be unique)

**Returns**: Tag ID, or 0 on failure

**Emits**: `tagsChanged()`

#### `bool renameTag(int id, const QString& name)`
Rename an existing tag.

**Parameters**:
- `id`: Tag ID
- `name`: New tag name

**Returns**: `true` on success, `false` on failure

**Emits**: `tagsChanged()`

#### `bool deleteTag(int id)`
Delete a tag (removes from all assets).

**Parameters**:
- `id`: Tag ID

**Returns**: `true` on success, `false` on failure

**Emits**: `tagsChanged()`

#### `bool mergeTags(int sourceTagId, int targetTagId)`
Merge two tags (all assets with source tag get target tag, source is deleted).

**Parameters**:
- `sourceTagId`: Source tag ID (will be deleted)
- `targetTagId`: Target tag ID (will be kept)

**Returns**: `true` on success, `false` on failure

**Emits**: `tagsChanged()`

#### `QVector<QPair<int, QString>> listTags() const`
Get all tags.

**Returns**: Vector of (tag ID, tag name) pairs

#### `bool assignTagsToAssets(const QList<int>& assetIds, const QList<int>& tagIds)`
Assign tags to assets (replaces existing tags).

**Parameters**:
- `assetIds`: List of asset IDs
- `tagIds`: List of tag IDs to assign

**Returns**: `true` on success, `false` on failure

**Emits**: `assetsChanged(folderId)` for each affected folder

#### `QStringList tagsForAsset(int assetId) const`
Get all tag names for an asset.

**Parameters**:
- `assetId`: Asset ID

**Returns**: List of tag names

### Database Management

#### `bool exportDatabase(const QString& filePath)`
Export the database to a file (backup).

**Parameters**:
- `filePath`: Destination file path

**Returns**: `true` on success, `false` on failure

#### `bool importDatabase(const QString& filePath)`
Import a database from a file (replaces current database).

**Parameters**:
- `filePath`: Source file path

**Returns**: `true` on success, `false` on failure

**Emits**: `foldersChanged()`, `tagsChanged()`

#### `bool clearAllData()`
Clear all data from the database (keeps schema).

**Returns**: `true` on success, `false` on failure

**Emits**: `foldersChanged()`, `tagsChanged()`

### Signals

#### `void foldersChanged()`
Emitted when folder structure changes.

#### `void assetsChanged(int folderId)`
Emitted when assets in a folder change.

**Parameters**:
- `folderId`: ID of the affected folder

#### `void tagsChanged()`
Emitted when tags change.

---

## AssetsModel Class

**Header**: `native/qt6/src/assets_model.h`

**Purpose**: Data model for asset grid/list views with filtering

**Base Class**: `QAbstractListModel`

### Enums

#### `enum Roles`
Data roles for accessing asset information:
- `IdRole`: Asset ID (int)
- `FileNameRole`: File name (QString)
- `FilePathRole`: Full file path (QString)
- `FileSizeRole`: File size in bytes (qint64)
- `ThumbnailPathRole`: Thumbnail cache path (QString)
- `FileTypeRole`: File extension (QString)
- `LastModifiedRole`: Modification timestamp (QDateTime)
- `RatingRole`: Rating 0-5 (int)
- `IsSequenceRole`: Is image sequence (bool)
- `SequencePatternRole`: Sequence pattern (QString)
- `SequenceStartFrameRole`: Start frame number (int)
- `SequenceEndFrameRole`: End frame number (int)
- `SequenceFrameCountRole`: Total frames (int)

### Constructor

#### `explicit AssetsModel(QObject* parent = nullptr)`
Create a new assets model.

### Properties

#### `int folderId() const`
#### `void setFolderId(int id)`
Get/set the current folder ID. Setting triggers a reload.

**Emits**: `folderIdChanged()`

#### `QString searchQuery() const`
#### `void setSearchQuery(const QString& query)`
Get/set the search filter (file name).

**Emits**: `searchQueryChanged()`

#### `int typeFilter() const`
#### `void setTypeFilter(int filter)`
Get/set the file type filter.

**Values**:
- `All`: All file types
- `Images`: Image files only
- `Videos`: Video files only
- `Audio`: Audio files only

**Emits**: `typeFilterChanged()`

#### `int ratingFilter() const`
#### `void setRatingFilter(int minRating)`
Get/set the minimum rating filter.

**Values**: 0-5 (0 = all ratings)

#### `QStringList selectedTagNames() const`
#### `void setSelectedTagNames(const QStringList& tags)`
Get/set the tag filter.

**Emits**: `selectedTagNamesChanged()`

#### `int tagFilterMode() const`
#### `void setTagFilterMode(int mode)`
Get/set the tag filter mode.

**Values**:
- `And`: Assets must have ALL selected tags
- `Or`: Assets must have ANY selected tag

**Emits**: `tagFilterModeChanged()`

### Methods

#### `int rowCount(const QModelIndex& parent) const override`
Get the number of filtered assets.

**Returns**: Number of assets matching current filters

#### `QVariant data(const QModelIndex& idx, int role) const override`
Get data for an asset.

**Parameters**:
- `idx`: Model index
- `role`: Data role (see Roles enum)

**Returns**: Data for the requested role

#### `bool moveAssetToFolder(int assetId, int folderId)`
Move a single asset to a folder.

**Parameters**:
- `assetId`: Asset ID
- `folderId`: Target folder ID

**Returns**: `true` on success

#### `bool moveAssetsToFolder(const QVariantList& assetIds, int folderId)`
Move multiple assets to a folder.

**Parameters**:
- `assetIds`: List of asset IDs
- `folderId`: Target folder ID

**Returns**: `true` on success

#### `bool removeAssets(const QVariantList& assetIds)`
Delete multiple assets.

**Parameters**:
- `assetIds`: List of asset IDs

**Returns**: `true` on success

#### `bool setAssetsRating(const QVariantList& assetIds, int rating)`
Set rating for multiple assets.

**Parameters**:
- `assetIds`: List of asset IDs
- `rating`: Rating value (0-5, or -1 to clear)

**Returns**: `true` on success

#### `bool assignTags(const QVariantList& assetIds, const QVariantList& tagIds)`
Assign tags to assets.

**Parameters**:
- `assetIds`: List of asset IDs
- `tagIds`: List of tag IDs

**Returns**: `true` on success

#### `QVariantMap get(int row) const`
Get all data for an asset as a map.

**Parameters**:
- `row`: Row index

**Returns**: Map of role names to values

#### `QStringList tagsForAsset(int assetId) const`
Get tag names for an asset.

**Parameters**:
- `assetId`: Asset ID

**Returns**: List of tag names

### Slots

#### `void reload()`
Reload assets from database and reapply filters.

### Signals

#### `void folderIdChanged()`
#### `void searchQueryChanged()`
#### `void typeFilterChanged()`
#### `void selectedTagNamesChanged()`
#### `void tagFilterModeChanged()`
Property change signals.

#### `void tagsChangedForAsset(int assetId)`
Emitted when tags change for an asset.

---

## VirtualFolderTreeModel Class

**Header**: `native/qt6/src/virtual_folders.h`

**Purpose**: Hierarchical tree model for virtual folders

**Base Class**: `QAbstractItemModel`

### Enums

#### `enum Roles`
- `IdRole`: Folder ID (int)
- `NameRole`: Folder name (QString)
- `DepthRole`: Depth in tree (int, 0 = root)
- `HasChildrenRole`: Has child folders (bool)

### Constructor

#### `explicit VirtualFolderTreeModel(QObject* parent = nullptr)`
Create a new folder tree model.

### Methods

#### `QModelIndex index(int row, int column, const QModelIndex& parent) const override`
Get model index for a folder.

#### `QModelIndex parent(const QModelIndex& child) const override`
Get parent index for a folder.

#### `int rowCount(const QModelIndex& parent) const override`
Get number of child folders.

#### `int columnCount(const QModelIndex& parent) const override`
Get number of columns (always 1).

#### `QVariant data(const QModelIndex& idx, int role) const override`
Get data for a folder.

#### `QString nodeName(int id) const`
Get folder name by ID.

**Parameters**:
- `id`: Folder ID

**Returns**: Folder name, or empty string if not found

### Slots

#### `void reload()`
Reload folder tree from database.

---

## TagsModel Class

**Header**: `native/qt6/src/tags_model.h`

**Purpose**: List model for tags

**Base Class**: `QAbstractListModel`

### Enums

#### `enum Roles`
- `IdRole`: Tag ID (int)
- `NameRole`: Tag name (QString)

### Constructor

#### `explicit TagsModel(QObject* parent = nullptr)`
Create a new tags model.

### Methods

#### `int rowCount(const QModelIndex& parent) const override`
Get number of tags.

#### `QVariant data(const QModelIndex& idx, int role) const override`
Get data for a tag.

#### `int createTag(const QString& name)`
Create a new tag.

**Returns**: Tag ID, or 0 on failure

#### `bool renameTag(int id, const QString& name)`
Rename a tag.

**Returns**: `true` on success

#### `bool deleteTag(int id)`
Delete a tag.

**Returns**: `true` on success

### Slots

#### `void reload()`
Reload tags from database.

---

## ThumbnailGenerator Class

**Header**: `native/qt6/src/thumbnail_generator.h`

**Purpose**: Singleton for asynchronous thumbnail generation

**Base Class**: `QObject`

### Static Methods

#### `static ThumbnailGenerator& instance()`
Get the singleton instance.

### Methods

#### `QString getThumbnailPath(const QString& filePath)`
Get thumbnail path for a file (returns empty if not cached).

**Parameters**:
- `filePath`: Source file path

**Returns**: Thumbnail path, or empty string if not cached

#### `void requestThumbnail(const QString& filePath)`
Request thumbnail generation (asynchronous).

**Parameters**:
- `filePath`: Source file path

**Emits**: `thumbnailGenerated()` when complete

#### `bool isImageFile(const QString& filePath)`
Check if file is an image.

#### `bool isVideoFile(const QString& filePath)`
Check if file is a video.

#### `bool isQtSupportedFormat(const QString& filePath)`
Check if Qt can load the format natively.

#### `void clearCache()`
Clear all cached thumbnails from disk.

#### `void startProgress(int total)`
Start progress tracking.

#### `void updateProgress()`
Increment progress counter.

#### `void finishProgress()`
Finish progress tracking.

### Signals

#### `void thumbnailGenerated(const QString& filePath, const QString& thumbnailPath)`
Emitted when thumbnail is generated.

#### `void thumbnailFailed(const QString& filePath)`
Emitted when thumbnail generation fails.

#### `void progressChanged(int current, int total)`
Emitted when progress changes.

---

## PreviewOverlay Class

**Header**: `native/qt6/src/preview_overlay.h`

**Purpose**: Full-screen preview widget for images, videos, and sequences

**Base Class**: `QWidget`

### Constructor

#### `explicit PreviewOverlay(QWidget *parent = nullptr)`
Create a new preview overlay.

### Methods

#### `void showAsset(const QString &filePath, const QString &fileName, const QString &fileType)`
Show an asset in preview mode.

**Parameters**:
- `filePath`: Path to the asset
- `fileName`: Display name
- `fileType`: File extension

#### `void showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame)`
Show an image sequence in preview mode.

**Parameters**:
- `framePaths`: List of frame file paths
- `sequenceName`: Display name
- `startFrame`: First frame number
- `endFrame`: Last frame number

#### `void navigateNext()`
Navigate to next asset.

**Emits**: `navigateRequested(1)`

#### `void navigatePrevious()`
Navigate to previous asset.

**Emits**: `navigateRequested(-1)`

#### `void stopPlayback()`
Stop video/sequence playback.

### Signals

#### `void closed()`
Emitted when preview is closed.

#### `void navigateRequested(int delta)`
Emitted when navigation is requested.

**Parameters**:
- `delta`: Direction (+1 = next, -1 = previous)

---

## OIIOImageLoader Class

**Header**: `native/qt6/src/oiio_image_loader.h`

**Purpose**: OpenImageIO integration for professional image formats

### Enums

#### `enum class ColorSpace`
- `Linear`: No color correction
- `sRGB`: Standard RGB color space
- `Rec709`: Broadcast standard

### Static Methods

#### `static QImage loadImage(const QString& filePath, int maxWidth = 0, int maxHeight = 0, ColorSpace colorSpace = ColorSpace::sRGB)`
Load an image using OpenImageIO.

**Parameters**:
- `filePath`: Path to image file
- `maxWidth`: Maximum width (0 = no limit)
- `maxHeight`: Maximum height (0 = no limit)
- `colorSpace`: Color space for HDR display

**Returns**: QImage, or null QImage on failure

#### `static bool isOIIOSupported(const QString& filePath)`
Check if file format is supported by OIIO.

**Returns**: `true` if supported

#### `static QImage toneMapHDR(const float* data, int width, int height, int channels, ColorSpace colorSpace = ColorSpace::sRGB, float exposure = 0.0f)`
Apply tone mapping to HDR image data.

**Parameters**:
- `data`: Float image data (RGB or RGBA)
- `width`: Image width
- `height`: Image height
- `channels`: Number of channels (3 or 4)
- `colorSpace`: Target color space
- `exposure`: Exposure adjustment

**Returns**: Tone-mapped 8-bit QImage

---

## AssetsTableModel Class

**Header**: `native/qt6/src/assets_table_model.h`

**Purpose**: Table adapter for AssetsModel (for QTableView)

**Base Class**: `QAbstractTableModel`

### Constructor

#### `explicit AssetsTableModel(AssetsModel* sourceModel, QObject* parent = nullptr)`
Create a table adapter.

**Parameters**:
- `sourceModel`: Source AssetsModel to wrap

### Methods

#### `int rowCount(const QModelIndex& parent) const override`
Get number of rows (assets).

#### `int columnCount(const QModelIndex& parent) const override`
Get number of columns (5: Name, Type, Size, Date, Rating).

#### `QVariant data(const QModelIndex& index, int role) const override`
Get data for a cell.

#### `QVariant headerData(int section, Qt::Orientation orientation, int role) const override`
Get column headers.

#### `void sort(int column, Qt::SortOrder order) override`
Sort by column.

---

## Usage Examples

### Loading Assets

```cpp
// Set folder and load assets
assetsModel->setFolderId(folderId);

// Apply filters
assetsModel->setSearchQuery("render");
assetsModel->setRatingFilter(3); // 3 stars and above
assetsModel->setSelectedTagNames({"character", "final"});
assetsModel->setTagFilterMode(AssetsModel::And);
```

### Creating Folders

```cpp
int parentId = 1; // Root folder
int newFolderId = DB::instance().createFolder("My Project", parentId);
if (newFolderId > 0) {
    qDebug() << "Created folder with ID:" << newFolderId;
}
```

### Generating Thumbnails

```cpp
// Request thumbnail
ThumbnailGenerator::instance().requestThumbnail("/path/to/image.jpg");

// Connect to signal
connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailGenerated,
        this, [](const QString& filePath, const QString& thumbnailPath) {
    qDebug() << "Thumbnail generated:" << thumbnailPath;
});
```

### Loading HDR Images

```cpp
// Load EXR with sRGB color space
QImage image = OIIOImageLoader::loadImage("/path/to/image.exr", 0, 0, 
                                          OIIOImageLoader::ColorSpace::sRGB);
if (!image.isNull()) {
    // Display image
}
```

---

## Thread Safety

- **DB**: Thread-safe (uses Qt's database connection per thread)
- **ThumbnailGenerator**: Thread-safe (uses mutex for pending set)
- **Models**: Not thread-safe (must be used from main thread only)
- **OIIOImageLoader**: Thread-safe (static methods, no shared state)

---

## Memory Management

All classes follow Qt's parent-child ownership model:
- Objects with a parent are automatically deleted when parent is deleted
- Models should be created with a parent (usually the view or main window)
- Singletons (DB, ThumbnailGenerator) manage their own lifetime

---

## Error Handling

- Database operations return `false` or `0` on failure
- Check return values and handle errors appropriately
- Use `qWarning()` or `qCritical()` for logging errors
- Display user-friendly error messages with `QMessageBox`

---

## See Also

- **[DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md)**: Development guidelines and architecture
- **[TECH.md](TECH.md)**: Technology stack and design decisions
- **[USER_GUIDE.md](USER_GUIDE.md)**: User-facing features and workflows

