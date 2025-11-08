# User Guide

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
- Grid/List toggle switches between thumbnails and details
- Keyboard: Arrow keys navigate assets; in preview/info modes use arrows for previous/next
- Drag-and-drop between folders mirrors Explorer behavior; dropping on a specific subfolder is allowed (dropping onto empty space of the same folder is blocked)

## Search and filtering

- Instant search by name
- Combine filters by tag (AND/OR), rating, and type
- Folders-first sorting (in both Grid and List) always lists folders before files

## Preview and playback
- Double-click to open Preview; right-click  e Preview also available
- Images: Zoom/Pan
- Videos and sequences: Timeline scrub; hold Ctrl over a grid card to scrub when enabled
- HDR/EXR: Basic color space selection (Linear, sRGB, Rec.709)
- Closing full-size preview restores focus/selection to the previously selected item for immediate keyboard navigation

## Tags and ratings
- Right-click assets  e Assign Tag / Set Rating
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
- Video formats: MOV, MP4, AVI; Images: PNG/JPG/TIF; HDR/EXR/PSD via OpenImageIO when available
- ProRes 4444 and Animation MOV conversions preserve alpha (where input provides alpha)
- PNG/TIF image sequence conversions preserve alpha
- Pause/Resume is intentionally disabled for conversions by design

## File operations and safety

- Copy/Move/Delete use the OS (Explorer/Shell) handlers; Recycle Bin is used for deletes when available
- Explorer-style context menu and drag-and-drop are used throughout; keyboard shortcuts are configurable in Settings
- Verify button (icons/Verify.png) runs a full directory verification scan on demand

## Logging and diagnostics
- Log Viewer (Help  e Logs) shows recent messages (ring buffer) and writes to app.log next to the executable
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
- Advanced formats (EXR/PSD/HDR): Install OpenImageIO via vcpkg or use a package with OIIO included

