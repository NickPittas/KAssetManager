# MainWindow.cpp Modularization Plan

## Executive Summary

**YES, mainwindow.cpp is too big.**

- **Current Size**: 8,313 lines, 343 KB
- **Current Complexity**: 109 member functions, 7 embedded helper classes
- **Recommendation**: **CRITICAL** - Immediate modularization required

This file has grown beyond maintainable size and violates the Single Responsibility Principle. It handles at least 5 distinct major subsystems that should be separated into dedicated modules.

---

## Current File Structure Analysis

### Size Breakdown by Section

| Section | Line Range | Lines | Percentage | Description |
|---------|------------|-------|------------|-------------|
| Includes & Utilities | 1-157 | 157 | 2% | Headers and namespace setup |
| Icon Helpers | 159-406 | 247 | 3% | Icon generation functions |
| SequenceGroupingProxyModel | 407-643 | 237 | 3% | Sequence detection proxy |
| Helper Classes | 644-1557 | 913 | 11% | Custom delegates, views, controllers |
| Constructor/Destructor | 1558-1691 | 134 | 2% | Object initialization |
| **setupUi (Asset Manager)** | 1692-2511 | 820 | 10% | Asset Manager UI construction |
| **setupFileManagerUi** | 2512-3410 | 899 | 11% | File Manager UI construction |
| **File Manager Functions** | 3411-4237 | 827 | 10% | File Manager slot implementations |
| setupConnections | 4238-4396 | 159 | 2% | Signal/slot connections |
| **Asset Manager Functions** | 4397-~6900 | ~2500 | 30% | Asset operations, tags, preview |
| **File Manager Preview** | ~7000-8200 | ~1200 | 14% | FM preview handling |
| Navigation & Misc | ~8200-8313 | ~113 | 1% | Navigation helpers |

### Function Distribution

- **File Manager Functions**: 28 onFm* slots + 6 helpers = 34 functions (31%)
- **Asset Manager Functions**: 15+ functions (18%)
- **Preview/Display Functions**: 12 functions (11%)
- **Tag Management**: 5 functions (5%)
- **Import/Project Operations**: 16 functions (15%)
- **Setup Functions**: 3 large functions (13%)
- **Selection Management**: 6 functions (5%)
- **Other**: 13 functions (12%)

### Embedded Helper Classes (Currently in .cpp)

1. `SequenceGroupingProxyModel` (237 lines) - Sequence detection and grouping
2. `AssetGridView` (60 lines) - Custom drag-and-drop view
3. `FmIconProvider` (23 lines) - File Manager icon provider
4. `AssetItemDelegate` (135 lines) - Asset grid rendering delegate
5. `FmItemDelegate` (146 lines) - File Manager grid delegate
6. `GridScrubOverlay` (112 lines) - Preview scrubbing overlay widget
7. `GridScrubController` (435 lines) - Preview scrubbing controller

**Total helper class code**: ~1,148 lines (14% of file)

---

## Problems with Current Structure

### 1. **Maintainability Crisis**
- Developers must navigate 8,313 lines to find/modify code
- High risk of unintended side effects when making changes
- Difficult to reason about state and dependencies
- Long compilation times when any change is made

### 2. **Violation of Single Responsibility Principle**
MainWindow currently handles:
- Asset Manager UI and logic
- File Manager UI and logic (complete file browser)
- Preview system (images, video, PDF, Office docs)
- Tag management system
- Import workflow
- Project folder watching
- Version control UI
- Database health monitoring
- Search integration

### 3. **Testing Difficulties**
- Cannot unit test individual subsystems in isolation
- All functionality requires full MainWindow instantiation
- Mock/stub creation is impractical

### 4. **Code Reusability**
- File Manager could be reused in other applications
- Preview system is generic enough for reuse
- Currently impossible to extract without full MainWindow

### 5. **Team Collaboration Issues**
- High risk of merge conflicts
- Multiple developers cannot work on different features simultaneously
- Code review requires understanding entire 8K+ line file

---

## Proposed Modularization Strategy

### Phase 1: Extract Helper Classes (Low Risk)
**Priority**: HIGH | **Risk**: LOW | **Estimated LOC Reduction**: ~1,200 lines

Extract the 7 embedded helper classes to separate files:

#### 1.1 Sequence Grouping Module
**Files to Create:**
- `src/widgets/sequence_grouping_proxy_model.h` (50 lines)
- `src/widgets/sequence_grouping_proxy_model.cpp` (200 lines)

**What to Extract:**
- `SequenceGroupingProxyModel` class (lines 407-643)
- Helper function `isImageFile()` if used only by this class

**Dependencies:**
- SequenceDetector (already exists)
- QFileSystemModel, QSortFilterProxyModel

**Why Safe:**
- Self-contained class with clear interface
- No direct MainWindow dependencies
- Already encapsulated

#### 1.2 Custom View Widgets
**Files to Create:**
- `src/widgets/asset_grid_view.h` (30 lines)
- `src/widgets/asset_grid_view.cpp` (50 lines)

**What to Extract:**
- `AssetGridView` class (lines 645-704)

**Dependencies:**
- QListView, QDrag

#### 1.3 Icon Provider
**Files to Create:**
- `src/widgets/fm_icon_provider.h` (20 lines)
- `src/widgets/fm_icon_provider.cpp` (40 lines)

**What to Extract:**
- `FmIconProvider` class (lines 706-728)

**Dependencies:**
- QFileIconProvider
- LivePreviewManager

#### 1.4 Item Delegates
**Files to Create:**
- `src/widgets/asset_item_delegate.h` (40 lines)
- `src/widgets/asset_item_delegate.cpp` (120 lines)
- `src/widgets/fm_item_delegate.h` (40 lines)
- `src/widgets/fm_item_delegate.cpp` (160 lines)

**What to Extract:**
- `AssetItemDelegate` class (lines 729-863)
- `FmItemDelegate` class (lines 864-1009)

**Dependencies:**
- QStyledItemDelegate, QPainter
- AssetsModel, SequenceGroupingProxyModel
- LivePreviewManager, OiioImageLoader

#### 1.5 Grid Scrubbing System
**Files to Create:**
- `src/widgets/grid_scrub_overlay.h` (40 lines)
- `src/widgets/grid_scrub_overlay.cpp` (90 lines)
- `src/widgets/grid_scrub_controller.h` (50 lines)
- `src/widgets/grid_scrub_controller.cpp` (400 lines)

**What to Extract:**
- `GridScrubOverlay` class (lines 1010-1121)
- `GridScrubController` class (lines 1122-1557)

**Dependencies:**
- QWidget, QObject
- QMediaPlayer, QVideoWidget

#### 1.6 Icon Helper Functions
**Files to Create:**
- `src/ui/icon_helpers.h` (40 lines)
- `src/ui/icon_helpers.cpp` (250 lines)

**What to Extract:**
- All icon generation functions (lines 159-406)
- `mkIcon()`, `loadPngIcon()`, `icoFolderNew()`, `icoCopy()`, etc.

**Dependencies:**
- QIcon, QPixmap, QPainter

---

### Phase 2: Extract File Manager Subsystem (Medium Risk)
**Priority**: HIGH | **Risk**: MEDIUM | **Estimated LOC Reduction**: ~3,500 lines

The File Manager is a complete file browser that can be its own module.

#### 2.1 File Manager Widget
**Files to Create:**
- `src/widgets/file_manager_widget.h` (~150 lines)
- `src/widgets/file_manager_widget.cpp` (~2,800 lines)

**What to Extract:**

**UI Setup (from setupFileManagerUi):**
- Lines 2512-3410 (~899 lines)
- All File Manager UI construction
- Toolbar, splitters, views, preview panel, info panel

**Slot Implementations:**
- Lines 3411-4159 (~750 lines)
- All `onFm*` functions (28 functions):
  - `onFmTreeActivated`, `onFmItemDoubleClicked`
  - `onFmCopy`, `onFmCut`, `onFmPaste`, `onFmDelete`, `onFmDeletePermanent`
  - `onFmRename`, `onFmBulkRename`, `onFmNewFolder`
  - `onFmAddToFavorites`, `onFmRemoveFavorite`, `onFmFavoriteActivated`
  - `onFmShowContextMenu`, `onFmTreeContextMenu`
  - `onFmViewModeToggled`, `onFmThumbnailSizeChanged`
  - `onFmGroupSequencesToggled`, `onFmHideFoldersToggled`
  - Navigation: `onFmNavigateBack`, `onFmNavigateUp`, etc.

**Helper Functions:**
- `fmPathForIndex`, `getSelectedFmTreePaths`
- `fmNavigateToPath`, `fmUpdateNavigationButtons`, `fmScrollTreeToPath`
- `updateFmInfoPanel`, `updateFmPreviewForIndex`, `clearFmPreview`
- `loadFmFavorites`, `saveFmFavorites`
- `applyFmShortcuts`, `fmShortcutFor`
- `doPermanentDelete`, `releaseAnyPreviewLocksForPaths`

**File Manager Preview System:**
- Lines ~7054-~7800 (~750 lines)
- `onFmSelectionChanged`, `onFmTogglePreview`, `onFmOpenOverlay`
- `changeFmPreview`
- Preview rendering for: images, video, audio, PDF, CSV, text, SVG, Office docs

**Navigation Implementation:**
- Lines ~8206-8313 (~107 lines)

**Member Variables to Move:**
- All `fm*` prefixed members (~50 variables from mainwindow.h lines 294-391)
- `fmSplitter`, `fmProxyModel`, `fmTree`, `fmTreeModel`
- `fmDirModel`, `fmGridView`, `fmListView`
- `fmPreviewPanel`, `fmImageView`, `fmVideoWidget`
- `fmInfoPanel`, `fmFavoritesList`
- `fmNavigationHistory`, `fmNavigationIndex`
- `fmClipboard`, `fmClipboardCutMode`
- All preview-related widgets

**Public Interface:**
```cpp
class FileManagerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileManagerWidget(QWidget *parent = nullptr);
    ~FileManagerWidget();

    // Public API for integration
    void navigateToPath(const QString& path);
    QString currentPath() const;
    QStringList selectedPaths() const;

signals:
    void fileActivated(const QString& path);
    void selectionChanged(const QStringList& paths);
    void addToLibraryRequested(const QStringList& paths);

private:
    // All FM-specific members and methods
};
```

**Dependencies:**
- QFileSystemModel, QFileSystemWatcher
- SequenceGroupingProxyModel (Phase 1)
- FmIconProvider, FmItemDelegate (Phase 1)
- LivePreviewManager
- FileOpsProgressDialog
- BulkRenameDialog

**Integration in MainWindow:**
```cpp
// In mainwindow.h - Replace ~50 fm* members with:
FileManagerWidget *fileManagerWidget;

// In setupFileManagerUi() - Replace 899 lines with:
fileManagerWidget = new FileManagerWidget(this);
fileManagerPage->layout()->addWidget(fileManagerWidget);
connect(fileManagerWidget, &FileManagerWidget::addToLibraryRequested,
        this, &MainWindow::onAddSelectionToAssetLibrary);
```

**Benefits:**
- Reduces mainwindow.cpp by ~3,500 lines (42%)
- File Manager becomes reusable component
- Can be tested independently
- Cleaner separation of concerns

---

### Phase 3: Extract Asset Manager UI Setup (Low-Medium Risk)
**Priority**: MEDIUM | **Risk**: LOW-MEDIUM | **Estimated LOC Reduction**: ~800 lines

#### 3.1 Asset Manager UI Builder
**Files to Create:**
- `src/ui/asset_manager_ui_builder.h` (~80 lines)
- `src/ui/asset_manager_ui_builder.cpp` (~800 lines)

**What to Extract:**
- Lines 1692-2511 (setupUi function)

**Approach:**
```cpp
class AssetManagerUiBuilder
{
public:
    struct Components {
        QTreeView *folderTreeView;
        QListView *assetGridView;
        QTableView *assetTableView;
        QWidget *filtersPanel;
        QWidget *infoPanel;
        // ... all UI components
    };

    static Components buildAssetManagerUi(QWidget *parent);

private:
    static QWidget* createFolderPanel(QTreeView **outTreeView);
    static QWidget* createAssetViewPanel(/* ... */);
    static QWidget* createFiltersPanel(/* ... */);
    static QWidget* createInfoPanel(/* ... */);
    static QWidget* createToolbar(/* ... */);
};
```

**In MainWindow::setupUi():**
```cpp
void MainWindow::setupUi()
{
    // Asset Manager page
    auto amComponents = AssetManagerUiBuilder::buildAssetManagerUi(assetManagerPage);

    // Assign to members
    folderTreeView = amComponents.folderTreeView;
    assetGridView = amComponents.assetGridView;
    // ... etc
}
```

**Why This Helps:**
- setupUi becomes readable (~50 lines instead of 820)
- UI construction logic is testable
- Can generate different layouts for different contexts

---

### Phase 4: Extract Tag Management Subsystem (Low Risk)
**Priority**: MEDIUM | **Risk**: LOW | **Estimated LOC Reduction**: ~400 lines

#### 4.1 Tag Manager Component
**Files to Create:**
- `src/components/tag_manager.h` (~60 lines)
- `src/components/tag_manager.cpp` (~350 lines)

**What to Extract:**

**Functions:**
- `onCreateTag()` (lines ~5405-5420)
- `onApplyTags()` (lines ~5420-5459)
- `onFilterByTags()` (lines ~5459-5541)
- `onTagContextMenu()` (lines ~5541-5623)
- `updateTagButtonStates()` (lines ~5623-5635)

**Members:**
- `tagsListView`, `tagsModel`
- `applyTagsBtn`, `filterByTagsBtn`
- `tagFilterModeCombo`

**Public Interface:**
```cpp
class TagManager : public QObject
{
    Q_OBJECT
public:
    explicit TagManager(QWidget *parent = nullptr);

    QWidget* createTagPanel();
    void setSelectedAssets(const QSet<int>& assetIds);
    QSet<int> getFilteredTagIds() const;

signals:
    void tagsApplied(const QSet<int>& assetIds, const QSet<int>& tagIds);
    void filterRequested(const QSet<int>& tagIds, bool matchAll);

private slots:
    void onCreateTag();
    void onApplyTags();
    void onFilterByTags();
    void onTagContextMenu(const QPoint& pos);
};
```

---

### Phase 5: Extract Preview System (Medium Risk)
**Priority**: LOW | **Risk**: MEDIUM | **Estimated LOC Reduction**: ~1,000 lines

#### 5.1 Preview Manager Component
**Files to Create:**
- `src/components/preview_manager.h` (~80 lines)
- `src/components/preview_manager.cpp` (~900 lines)

**What to Extract:**

**Functions:**
- `showPreview()` (lines ~4766-4816)
- `closePreview()` (lines ~4816-4859)
- `changePreview()` (lines ~4859-4870)
- Preview-related event handlers

**Members:**
- `previewOverlay`
- `assetScrubController`, `fmScrubController`
- `previewIndex`
- `versionPreviewCache`

**Note:** This is already partially modularized with PreviewOverlay class. This phase would further consolidate preview-related coordination logic.

---

### Phase 6: Extract Import Workflow (Low Risk)
**Priority**: LOW | **Risk**: LOW | **Estimated LOC Reduction**: ~300 lines

#### 6.1 Import Coordinator
**Files to Create:**
- `src/components/import_coordinator.h` (~50 lines)
- `src/components/import_coordinator.cpp` (~280 lines)

**What to Extract:**

**Functions:**
- `importFiles()` (lines ~5730-5751)
- `onImportProgress()` (lines ~5751-5762)
- `onImportFileChanged()` (lines ~5762-5770)
- `onImportFolderChanged()` (lines ~5770-5778)
- `onImportComplete()` (lines ~5778-5821)
- Drag-and-drop event handlers (lines ~5635-5730)

**Members:**
- `importer`
- `importProgressDialog`

---

### Phase 7: Extract Selection Management (Low Risk)
**Priority**: LOW | **Risk**: LOW | **Estimated LOC Reduction**: ~200 lines

#### 7.1 Asset Selection Manager
**Files to Create:**
- `src/components/asset_selection_manager.h` (~40 lines)
- `src/components/asset_selection_manager.cpp` (~180 lines)

**What to Extract:**

**Functions:**
- `getSelectedAssetIds()`, `getAnchorIndex()`
- `selectAsset()`, `selectSingle()`, `toggleSelection()`, `selectRange()`
- `clearSelection()`
- `onAssetSelectionChanged()`

**Members:**
- `selectedAssetIds`
- `anchorIndex`
- `currentAssetId`

---

## Implementation Steps for an Assistant

### PHASE 1: Helper Classes Extraction

#### Step 1.1: Extract SequenceGroupingProxyModel
1. **Create** `src/widgets/sequence_grouping_proxy_model.h`
   - Add header guards
   - Include necessary Qt headers
   - Declare class interface (lines 407-621 from mainwindow.cpp)

2. **Create** `src/widgets/sequence_grouping_proxy_model.cpp`
   - Move implementation
   - Add necessary includes

3. **Update** `mainwindow.cpp`
   - Remove class definition (lines 407-621)
   - Add `#include "widgets/sequence_grouping_proxy_model.h"`

4. **Update** `CMakeLists.txt` or project file
   - Add new source files to build

5. **Compile and test**
   - Verify File Manager sequence grouping still works
   - Test "Group Sequences" toggle
   - Test "Hide Folders" toggle

#### Step 1.2: Extract AssetGridView
1. **Create** `src/widgets/asset_grid_view.h`
2. **Create** `src/widgets/asset_grid_view.cpp`
3. **Update** `mainwindow.cpp`
   - Remove class (lines 645-704)
   - Add include
4. **Update** build system
5. **Compile and test**
   - Verify Asset Manager grid view works
   - Test drag-and-drop

#### Step 1.3-1.6: Repeat for remaining helper classes
Follow same pattern for:
- FmIconProvider
- AssetItemDelegate
- FmItemDelegate
- GridScrubOverlay
- GridScrubController
- Icon helper functions

**Testing checkpoint:**
- All Asset Manager features work
- All File Manager features work
- Preview scrubbing works
- No compilation errors
- **Expected result**: mainwindow.cpp reduced to ~7,100 lines

---

### PHASE 2: File Manager Extraction (MOST IMPACTFUL)

#### Step 2.1: Create FileManagerWidget skeleton

1. **Create** `src/widgets/file_manager_widget.h`
   ```cpp
   #ifndef FILE_MANAGER_WIDGET_H
   #define FILE_MANAGER_WIDGET_H

   #include <QWidget>
   #include <QSet>
   #include <QHash>
   #include <QString>
   #include <QStringList>
   #include <QModelIndex>

   // Forward declarations
   class QSplitter;
   class QTreeView;
   class QListView;
   class QTableView;
   class QListWidget;
   class QFileSystemModel;
   class QFileSystemWatcher;
   class QStackedWidget;
   class QGraphicsView;
   class QGraphicsScene;
   class QVideoWidget;
   class QMediaPlayer;
   class QAudioOutput;
   class QToolButton;
   class QSlider;
   class QLabel;
   class QPushButton;
   class QCheckBox;
   class QGraphicsPixmapItem;
   class QPlainTextEdit;
   class QStandardItemModel;
   class QPdfDocument;
   class QPdfView;
   class QGraphicsItem;
   class QListWidgetItem;
   class QShortcut;

   class SequenceGroupingProxyModel;
   class FileOpsProgressDialog;

   class FileManagerWidget : public QWidget
   {
       Q_OBJECT

   public:
       explicit FileManagerWidget(QWidget *parent = nullptr);
       ~FileManagerWidget();

       // Public API
       void navigateToPath(const QString& path);
       QString currentPath() const;
       QStringList selectedPaths() const;

   signals:
       void fileActivated(const QString& path);
       void selectionChanged(const QStringList& paths);
       void addToLibraryRequested(const QStringList& paths);

   private slots:
       // All onFm* slots from mainwindow.h lines 112-150

   private:
       void setupUi();
       void setupConnections();

       // Helper methods

       // All fm* member variables from mainwindow.h lines 294-391
   };

   #endif // FILE_MANAGER_WIDGET_H
   ```

2. **Create** `src/widgets/file_manager_widget.cpp`
   - Add includes
   - Implement constructor (initialize all members)
   - Implement destructor
   - Create empty implementations for all methods

#### Step 2.2: Move UI setup code

1. **Copy** setupFileManagerUi code from mainwindow.cpp (lines 2512-3410) to FileManagerWidget::setupUi()
2. **Adapt** code:
   - Change `this` references to refer to FileManagerWidget instead of MainWindow
   - Replace MainWindow member access with FileManagerWidget members
   - Update layout parent to be FileManagerWidget
3. **Update** MainWindow::setupFileManagerUi():
   ```cpp
   void MainWindow::setupFileManagerUi()
   {
       fileManagerWidget = new FileManagerWidget(this);

       auto layout = new QVBoxLayout(fileManagerPage);
       layout->setContentsMargins(0, 0, 0, 0);
       layout->addWidget(fileManagerWidget);
   }
   ```

#### Step 2.3: Move slot implementations

1. **Move** all onFm* function implementations from mainwindow.cpp to file_manager_widget.cpp
   - Lines 3411-4159 (~750 lines)
   - Update any MainWindow-specific calls to use signals instead

2. **Move** FM helper function implementations:
   - fmPathForIndex, getSelectedFmTreePaths
   - updateFmInfoPanel, updateFmPreviewForIndex, clearFmPreview
   - fmNavigateToPath, fmUpdateNavigationButtons, fmScrollTreeToPath
   - loadFmFavorites, saveFmFavorites
   - applyFmShortcuts, fmShortcutFor
   - doPermanentDelete, releaseAnyPreviewLocksForPaths

3. **Move** File Manager preview code (lines ~7054-7800)

4. **Move** Navigation implementation (lines ~8206-8313)

#### Step 2.4: Move member variables

1. **Remove** from mainwindow.h (lines 294-391):
   - All fm* member variables

2. **Add** to file_manager_widget.h:
   - All those same members as private members

3. **Update** mainwindow.h:
   ```cpp
   // Replace ~50 fm* members with single line:
   FileManagerWidget *fileManagerWidget;
   ```

#### Step 2.5: Connect signals

1. **In** MainWindow::setupConnections():
   ```cpp
   connect(fileManagerWidget, &FileManagerWidget::addToLibraryRequested,
           this, &MainWindow::onAddSelectionToAssetLibrary);
   ```

2. **Update** onAddSelectionToAssetLibrary if needed to work with signal data

#### Step 2.6: Update CMakeLists.txt

```cmake
set(SOURCES
    # ... existing ...
    src/widgets/file_manager_widget.cpp
)

set(HEADERS
    # ... existing ...
    src/widgets/file_manager_widget.h
)
```

#### Step 2.7: Compile and Test thoroughly

**Test File Manager features:**
- [ ] Tree navigation works
- [ ] Grid/List view switching works
- [ ] Thumbnail size slider works
- [ ] File operations work (copy, cut, paste, delete, rename)
- [ ] Bulk rename works
- [ ] New folder creation works
- [ ] Favorites add/remove/navigate works
- [ ] Context menus work (tree, grid, empty space)
- [ ] Preview panel shows content correctly:
  - [ ] Images (with alpha channel toggle)
  - [ ] Videos (playback controls)
  - [ ] Audio files
  - [ ] PDFs
  - [ ] CSV files
  - [ ] Text files
  - [ ] SVG files
  - [ ] Office documents (if applicable)
- [ ] Info panel updates correctly
- [ ] Sequence grouping toggle works
- [ ] Hide folders toggle works
- [ ] Back/Up navigation works
- [ ] Directory watching and auto-refresh works
- [ ] Double-click to activate files works
- [ ] "Add to Library" works from File Manager
- [ ] Keyboard shortcuts work
- [ ] Everything Search integration works

**Expected result**: mainwindow.cpp reduced to ~3,600 lines (57% reduction from current)

---

### PHASE 3: Asset Manager UI Builder Extraction

#### Step 3.1: Create AssetManagerUiBuilder

1. **Create** `src/ui/asset_manager_ui_builder.h`
   - Define Components struct
   - Declare static build methods

2. **Create** `src/ui/asset_manager_ui_builder.cpp`
   - Move setupUi code (lines 1692-2511)
   - Break into logical builder methods:
     - createFolderPanel()
     - createAssetViewPanel()
     - createFiltersPanel()
     - createInfoPanel()
     - createVersionPanel()
     - createToolbar()
     - buildAssetManagerUi() - orchestrates all above

3. **Update** MainWindow::setupUi() to use builder

4. **Compile and test**
   - Verify all Asset Manager UI appears correctly
   - Test all UI interactions

**Expected result**: mainwindow.cpp reduced to ~2,800 lines (66% reduction)

---

### PHASE 4: Tag Management Extraction

#### Step 4.1: Create TagManager component

1. **Create** `src/components/tag_manager.h`
2. **Create** `src/components/tag_manager.cpp`
3. **Move** tag-related code from mainwindow.cpp
4. **Update** MainWindow to use TagManager
5. **Connect** signals
6. **Test** tag operations

**Expected result**: mainwindow.cpp reduced to ~2,400 lines (71% reduction)

---

### PHASE 5-7: Additional Extractions (Optional)

Continue with Preview, Import, and Selection managers as needed.

**Final expected result**: mainwindow.cpp reduced to ~1,500-2,000 lines (75-82% reduction)

---

## Risk Mitigation Strategies

### 1. Incremental Approach
- Complete one phase at a time
- Test thoroughly after each phase
- Don't proceed to next phase until current is verified

### 2. Version Control
- Create feature branch: `refactor/mainwindow-modularize`
- Commit after each successful module extraction
- Use descriptive commit messages
- Tag stable points

### 3. Testing Strategy
- **Unit tests**: Create tests for each extracted module
- **Integration tests**: Verify MainWindow still works
- **Manual testing**: Test all UI features after each phase
- **Regression testing**: Run full application test suite

### 4. Preserve Functionality
- **DO NOT** change behavior during refactoring
- **DO NOT** fix bugs during extraction (do that separately)
- **DO NOT** add features during extraction
- **ONLY** move code and maintain existing functionality

### 5. Code Review Checkpoints
- Review after Phase 1 (helper classes)
- Review after Phase 2 (File Manager)
- Review after Phase 3 (UI builder)
- Get approval before proceeding to next phase

---

## File Organization After Refactoring

```
src/
├── mainwindow.h                          (~200 lines, reduced from 400)
├── mainwindow.cpp                        (~1,500 lines, reduced from 8,313)
├── widgets/
│   ├── sequence_grouping_proxy_model.h/cpp
│   ├── asset_grid_view.h/cpp
│   ├── fm_icon_provider.h/cpp
│   ├── asset_item_delegate.h/cpp
│   ├── fm_item_delegate.h/cpp
│   ├── grid_scrub_overlay.h/cpp
│   ├── grid_scrub_controller.h/cpp
│   └── file_manager_widget.h/cpp         (~2,800 lines)
├── components/
│   ├── tag_manager.h/cpp
│   ├── preview_manager.h/cpp
│   ├── import_coordinator.h/cpp
│   └── asset_selection_manager.h/cpp
└── ui/
    ├── icon_helpers.h/cpp
    └── asset_manager_ui_builder.h/cpp    (~800 lines)
```

---

## Expected Benefits

### 1. **Maintainability**
- MainWindow becomes manageable at ~1,500 lines
- Each module is <1,000 lines (except File Manager at ~2,800)
- Clear separation of concerns
- Easy to locate code

### 2. **Testability**
- Each module can be unit tested independently
- Mock interfaces are straightforward
- Integration testing is more focused

### 3. **Reusability**
- FileManagerWidget can be used in other applications
- Tag manager can be reused
- Preview system is portable

### 4. **Compilation Speed**
- Smaller files compile faster
- Changes to one module don't require recompiling everything

### 5. **Team Collaboration**
- Multiple developers can work on different modules
- Reduced merge conflicts
- Clearer code ownership

### 6. **Code Quality**
- Easier to review smaller modules
- Dependencies are explicit
- Architectural issues become visible

---

## Timeline Estimate (for a single developer)

| Phase | Estimated Time | Risk Level |
|-------|---------------|------------|
| Phase 1: Helper Classes | 2-3 days | Low |
| Phase 2: File Manager | 4-5 days | Medium |
| Phase 3: UI Builder | 1-2 days | Low |
| Phase 4: Tag Manager | 1 day | Low |
| Phase 5: Preview Manager | 2 days | Medium |
| Phase 6: Import Coordinator | 1 day | Low |
| Phase 7: Selection Manager | 1 day | Low |
| **Testing & Polish** | 2-3 days | - |
| **Total** | **14-19 days** | - |

**Recommended approach**: Complete Phases 1-4 first (9-11 days), which provides 70%+ of the benefit with lower risk. Evaluate if Phases 5-7 are needed.

---

## Conclusion

**YES, mainwindow.cpp must be refactored.** At 8,313 lines, it has exceeded reasonable maintainability limits by a factor of 5-10x.

**Priority Actions:**
1. **Immediate**: Start Phase 1 (Helper Classes) - Low risk, immediate benefit
2. **High Priority**: Complete Phase 2 (File Manager) - Highest impact, well-defined boundaries
3. **Medium Priority**: Complete Phase 3-4 (UI Builder, Tags) - Consolidate improvements
4. **Optional**: Phases 5-7 if time permits

**Success Criteria:**
- mainwindow.cpp reduced to <2,000 lines (target: ~1,500)
- All existing functionality preserved
- All tests passing
- No performance regression
- Improved code organization and maintainability

This refactoring is **essential** for the long-term health of the KAssetManager project.
