#include "user_guide_dialog.h"
#include "theme_manager.h"
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QPushButton>
#include <QScrollBar>

UserGuideDialog::UserGuideDialog(QWidget *parent)
    : QDialog(parent)
    , textBrowser(nullptr)
{
    setupUi();
    loadUserGuide();
}

UserGuideDialog::~UserGuideDialog()
{
}

void UserGuideDialog::setupUi()
{
    setWindowTitle("KAsset Manager - User Guide");
    resize(900, 700);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Text browser for markdown content
    textBrowser = new QTextBrowser(this);
    textBrowser->setOpenExternalLinks(true);
    textBrowser->setReadOnly(true);

    // Apply theme-aware styling
    bool isDark = ThemeManager::instance().currentTheme() == ThemeManager::Dark;
    QString bgColor = isDark ? "#2b2b2b" : "#ffffff";
    QString textColor = isDark ? "#e0e0e0" : "#000000";
    QString linkColor = isDark ? "#4a9eff" : "#0066cc";
    QString codeBlockBg = isDark ? "#1e1e1e" : "#f5f5f5";
    QString codeBorder = isDark ? "#404040" : "#d0d0d0";
    
    textBrowser->setStyleSheet(QString(
        "QTextBrowser {"
        "    background-color: %1;"
        "    color: %2;"
        "    border: none;"
        "    padding: 20px;"
        "    font-size: 11pt;"
        "    line-height: 1.6;"
        "}"
        "QTextBrowser a {"
        "    color: %3;"
        "    text-decoration: none;"
        "}"
        "QTextBrowser a:hover {"
        "    text-decoration: underline;"
        "}"
    ).arg(bgColor, textColor, linkColor));

    // Set document CSS for better markdown rendering
    textBrowser->document()->setDefaultStyleSheet(QString(
        "h1, h2, h3 { color: %1; margin-top: 20px; margin-bottom: 10px; }"
        "h1 { font-size: 24pt; border-bottom: 2px solid %2; padding-bottom: 8px; }"
        "h2 { font-size: 18pt; border-bottom: 1px solid %2; padding-bottom: 6px; }"
        "h3 { font-size: 14pt; }"
        "p { margin: 8px 0; }"
        "ul, ol { margin: 8px 0; padding-left: 30px; }"
        "li { margin: 4px 0; }"
        "code { "
        "    background-color: %3;"
        "    border: 1px solid %4;"
        "    padding: 2px 6px;"
        "    border-radius: 3px;"
        "    font-family: 'Consolas', 'Courier New', monospace;"
        "    font-size: 10pt;"
        "}"
        "pre { "
        "    background-color: %3;"
        "    border: 1px solid %4;"
        "    padding: 12px;"
        "    border-radius: 5px;"
        "    margin: 10px 0;"
        "    overflow-x: auto;"
        "}"
        "strong { font-weight: bold; }"
        "em { font-style: italic; }"
    ).arg(textColor, codeBorder, codeBlockBg, codeBorder));

    mainLayout->addWidget(textBrowser);

    // Close button
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(10, 10, 10, 10);
    buttonLayout->addStretch();
    
    QPushButton* closeButton = new QPushButton("Close", this);
    closeButton->setMinimumWidth(100);
    closeButton->setStyleSheet(ThemeManager::instance().buttonStyleSheet());
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);
}

void UserGuideDialog::loadUserGuide()
{
    // Embedded markdown content
    const QString markdownContent = R"(# User Guide

This guide covers day-to-day usage of KAsset Manager. The UI is designed to feel like Windows Explorer with a persistent folder tree and powerful preview, tagging, and conversion tools.

## Layout

- Left pane: Folder tree (always expanded; single-click to navigate)
- Right pane: Asset area (Grid/List view, toolbar, filters)
- Top toolbar (left-aligned): New Folder, Copy, Cut, Paste, Delete, Rename, Add to Library, List/Grid Toggle, Grid size, Group Sequences
- Top toolbar (right-aligned): Preview toggle
- Filters panel: Visible controls for Tags, Categories, Rating, File Types

## Importing and libraries

- Drag-and-drop files/folders anywhere in the window to import
- From File Manager, use Add to Library to import selected items; when folders are added, their subfolder hierarchy is preserved in the Asset Manager
- Project folders appear with a special icon, are immovable in the folder pane, and have editable names only
- Project folders have a watchdog; click Refresh to force a rescan if needed
- Asset Lock: Use the red "Locked" checkbox at the top to restrict moves to within a project. When unlocked, normal operations are allowed

## Navigation and selection
- Left pane selections only change what the right pane shows; the left tree does not open into the folder
- Selecting a new folder resets the asset list/grid scroll position to the top
- Expanding folders in the left tree only enumerates subfolders; it does not scan files or generate thumbnails until you select a folder.
- Grid/List toggle switches between thumbnails and details
- File Manager does not prefetch thumbnails or metadata while you scroll; previews and detailed metadata are loaded only for the currently selected item (and only when the Preview/Info panes are visible). List view columns continue to show basic file properties.
- Keyboard: Arrow keys navigate assets; in preview/info modes use arrows for previous/next
- Drag-and-drop between folders mirrors Explorer behavior; dropping on a specific subfolder is allowed (dropping onto empty space of the same folder is blocked)

## Search and filtering

- Instant search by name
- Combine filters by tag (AND/OR), rating, and type
- Folders-first sorting (in both Grid and List) always lists folders before files

## Preview and playback
- Double-click to open Preview; right-click → Preview also available
- Images: Zoom/Pan with mouse wheel and drag
- Videos and sequences: Timeline scrub; hold Ctrl over a grid card to scrub when enabled
- HDR/EXR: Basic color space selection (Linear, sRGB, Rec.709)
- Closing full-size preview restores focus/selection to the previously selected item for immediate keyboard navigation
- **Full-screen preview navigation**: Use left/right arrow keys to navigate between assets while in full-screen preview mode
- **Synchronized selection**: When navigating in full-screen preview, the Asset Manager grid/list automatically highlights the currently previewed asset in the background
- **Persistent selection**: When closing the full-screen preview (Esc or close button), the Asset Manager selection remains on the last previewed asset for immediate keyboard navigation

## Annotation tools
- Click the **Annotate** button (top bar) to enter annotation mode
- **Drawing tools**:
  - **Select tool** (cursor icon) - Move and resize existing annotations
  - **Pen tool** - Freehand drawing with customizable color and width (1-20px)
  - **Text tool** - Add text labels with font and color selection
  - **Rectangle tool** - Draw rectangles and squares
  - **Circle tool** - Draw circles and ellipses
  - **Arrow tool** - Draw directional arrows
- **Per-frame annotations**: Each frame of a video or sequence maintains its own annotations
- **Timeline markers**: Green lines on the timeline indicate frames with annotations
- **Frame navigation**: Use frame step buttons (comma/period keys) or timeline scrubbing while in annotation mode
- **Frame accuracy**: Annotations are locked to exact frame numbers, preventing drift when scrubbing
- **Undo/Redo**: Full undo/redo support (Ctrl+Z/Ctrl+Y)
- **Clear**: Remove all annotations from current frame
- **Save options**:
  - **Save Frame**: Export current annotated frame as PNG/JPG
  - **Save All**: Batch export all annotated frames with naming pattern `{filename}_annotation_{frame}.png`
- **Keyboard shortcuts**:
  - `A` - Toggle annotation mode
  - `Ctrl+Z` - Undo last annotation
  - `Ctrl+Y` - Redo annotation
  - `Del` - Delete selected annotation
  - `,` (comma) - Previous frame
  - `.` (period) - Next frame
  - `Esc` - Exit annotation mode

## Tags and ratings
- Right-click assets → Assign Tag / Set Rating
- Tags can be created, renamed, merged, or deleted; multiple tags per asset are supported
- 5-star rating system; filters can combine rating and tags

## Image sequences

- Numbered image sequences are detected automatically and can be grouped (toolbar: Group Sequences)
- First/Last frame detection is available in File Manager

## External drag-and-drop to other applications

- Windows Explorer/Desktop: Dragging an image sequence copies the individual frame files (not the parent folder)
- Nuke and After Effects: Dragging a sequence sends the sequence folder so both apps import a single sequence item
- File Manager and Asset Manager behave identically for external drag-and-drop

## Conversion

- Convert videos, image sequences, and single images via the Convert dialog
- Video formats: MOV, MP4, AVI; Images: PNG/JPG/TIF; All image formats handled via OpenImageIO for consistent quality
- ProRes 4444 and Animation MOV conversions preserve alpha (where input provides alpha)
- PNG/TIF image sequence conversions preserve alpha
- Pause/Resume is intentionally disabled for conversions by design

## File operations and safety

- Copy/Move/Delete use the OS (Explorer/Shell) handlers; Recycle Bin is used for deletes when available
- Explorer-style context menu and drag-and-drop are used throughout; keyboard shortcuts are configurable in Settings
- Verify button (icons/Verify.png) runs a full directory verification scan on demand

## Context menu actions

- **Show in Explorer**: Right-click any asset and select "Show in Explorer" to open Windows Explorer with that file selected
- **Generate Thumbnail**: Right-click any asset (image, video, or sequence) and select "Generate Thumbnail" to regenerate its thumbnail immediately
- These actions are available in both Asset Manager grid/list views

## Logging and diagnostics
- Log Viewer (Help → Logs) shows recent messages (ring buffer) and writes to app.log next to the executable
- Decoder/preview issues are labeled with [LivePreview] in logs; converter issues show [Convert]

## Data persistence

- Database and user data (tags, etc.) persist across app updates and installations; only removable by explicit user action

## Security and privacy

- Filenames are validated before rename/move
- External tool invocations (FFmpeg/ImageMagick) are hardened against flag injection
- Crash logs in release builds avoid disclosing raw memory addresses

## Troubleshooting

- Live preview delays on large files: Allow a moment for the first frame to decode; check logs
- Import or conversion failures: Ensure files are readable and not locked; verify sufficient disk space
- Image display issues: All image formats use OpenImageIO for consistent quality; ensure OIIO is available via vcpkg or included in the package
- Grayscale images: Now properly display in grayscale (not red-tinted) with correct channel replication
- HDR/PFM thumbnails: Now generate properly in File Manager grid view

## Getting help

- **Help menu**: Access the Help menu from the menu bar for quick access to documentation and application information
- **User Guide**: Select Help → User Guide (or press F1) to open this comprehensive user guide
- **About KAsset Manager**: Select Help → About KAsset Manager to view application version, author, license information, and links to documentation and the GitHub repository
)";

    // Set markdown content
    textBrowser->setMarkdown(markdownContent);

    // Scroll to top
    textBrowser->verticalScrollBar()->setValue(0);
}

