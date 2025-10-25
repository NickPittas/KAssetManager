$content = @"

## Latest updates (2025-10-25)
- DONE: Asset type filter (All/Images/Videos) added to AssetsModel with header buttons in Main.qml; filtering integrated with ThumbnailGenerator helpers.
- DONE: Tags panel under Folders with "+" dialog to name new tags; drag selected assets onto a tag to assign; DB schema tables (tags, asset_tags) and AssetsModel.assignTags wired.
- DONE: Right-click behavior fixed: per-asset context menu on tiles; bulk selection menu when right-clicking empty space in grid/list; no overlay blocks the search/filters.
- DONE: Grid tiles resized and clipped so metadata no longer overlaps; show name/type/size/modified and Tags.
- DONE: Info panel shows Tags for the selected asset; preview components stabilized.
- DONE: QSettings identifiers set in main.cpp so Settings persist.
- DONE: Build/packaging script verified; portable build output at dist/portable.

## Next actions
- Tag filtering in model and UI (multi-select, intersection/union modes).
- Ratings: add DB column migration + UI stars in grid/info + batch set + filtering.
- Bulk menu: add "Assign Tag…" submenu and quick rating actions parity in list view.
- Tests: add smoke tests for tag CRUD and assignment; add diagnostics for filter changes.
- Docs: add user guide section for Filters/Tags and update screenshots.
"@
Add-Content -LiteralPath "$PSScriptRoot/../TASKS.md" -Value $content
