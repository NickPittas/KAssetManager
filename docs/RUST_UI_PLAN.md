# KAssetManager UI Surface Plan (parity spec for the Rust UI phase)

Written before UI-side work begins, per the standing rule: no presentation work starts until this document exists
and acts as its spec. Everything here is derived from the Qt application in `native/qt6/src` (inventories with
`file:line` evidence), not from GalleryBrowser.

## Governing rules

1. **Parity target = every feature of Qt KAssetManager.** GalleryBrowser supplies the *engine* only: playback,
   thumbnails, frame scrubbing (`crates/media`, `crates/player`). Nothing else is inherited from it.
2. **Parity of capability, not implementation.** A feature must be doable; Qt's workaround machinery may shrink or
   go. Each row below carries a decision: **port** (same interaction), **replace** (engine/platform gives the same
   outcome with less UI), or **drop**.
3. Worked example of rule 2: Qt's thumbnail pre-generation dialog, worker and prefetch/refresh menu exist to prime
   decoding. The vendored `Thumbnailer` is a background service with memory LRU + disk cache keyed by
   `(path, size, mtime, max_edge)` (`crates/media/src/thumbs.rs:69-72`), so generation is on-demand and persistent.
   → **drop** the dialog/worker/menu; keep the cache budget setting; at most one "refresh thumbnails" action later.

Legend: `mw.cpp` = `native/qt6/src/mainwindow.cpp`; other Qt paths are relative to `native/qt6/src/`. Rust status is
from `apps/kassetmanager/src/app.rs` as of `c5a3829`.

## Top-level structure — the app is three pages, not one

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Tabbed pages: **File Manager**, **Asset Manager**, **Project Manager** | `mainTabs = QTabWidget`, `mw.cpp:1223/1226/1234` | one browser page | port (tabs) | P1 |
| Last tab remembered across runs | `mw.cpp:1357` | n/a | port | P1 |
| Each page owns its toolbar + grid/list stack | `mw.cpp:798, 1327, 1735, 2466` | single view mode in Settings | port | P1 |
| Status bar with progress + transient messages | `mw.cpp:345-352, 2900, 3493` | status line only | port | P1 |

**Consequence:** what the Rust app has today is one page (roughly Qt's File Manager page) without its chrome. The
Asset Manager page (library/tag/rating backed) and Project Manager page are still un-built surfaces.

## File Manager page — chrome and navigation

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Back / Up buttons, **no Forward**, no breadcrumb | `mw.cpp:678-693` (FM `1601-1613`) | breadcrumb + `..` row | replace (Back/Up + path bar) | P1 |
| Editable path bar, Enter navigates | `mw.cpp:1711-1732` | breadcrumb only | port | P1 |
| New Folder / Copy / Cut / Paste / Delete / Rename / Add-to-Library as toolbar buttons | `mw.cpp:1618-1634` | keyboard only, in Settings elsewhere | **move out of Settings → toolbar** | P1 |
| Grid/List view toggle button | `mw.cpp:1636-1642, 2466` | `view_mode` in Settings | move to toolbar | P1 |
| Thumbnail size slider (64-320 px FM; 100-400 Asset) | `mw.cpp:1645-1652, 716-730` | 4 discrete sizes in Settings | port slider | P1 |
| Group sequences / Hide folders toggles | `mw.cpp:1656-1672` | `collapse_sequences` in Settings; no hide-folders | move + add | P1 |
| Search entry point ("Search entire disk") | `mw.cpp:1675-1681` | none | port (backend is M7) | P5 |
| Preview panel show/hide toggle | `mw.cpp:1683-1691` | always shown | port | P1 |
| Dual-pane toggle creating a **full second pane** (own toolbar, own preview) | `mw.cpp:1693-1698, 3682-3760`; `file_manager_pane.cpp:144` | second pane shares app chrome; flag in Settings | move to toolbar; grow pane to full clone | P1 |
| Sync-navigation toggle (only when dual-pane) | `mw.cpp:1702-1708` | none | port | P1 |
| Favorites list, double-click opens, add/remove via context menu, persisted | `mw.cpp:1504-1526, 3073-3127, 3314` | `Settings::favorites` field exists, **no UI** | port list UI | P1 |
| Folder tree with real roots: "Computer" → `QDir::drives()`, lazy async expansion, drive icons | `everything_folder_model.cpp:46-50, 573-620, 297-300`; `mw.cpp:1408, 1530-1568` | subfolders of current dir only; no roots | port tree (roots + lazy) | P1 |
| Refresh (F5) + context-menu Refresh | `mw.cpp:2374, 3184` | F5 exists | port menu item | P1 |

## Views, icons, selection

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Multi-select everywhere (`ExtendedSelection`) | `mw.cpp:814, 834, 1765, 1832, 640` | **single** selection (`selected: Option<PathBuf>`) | port (blocking for ops at scale) | P1 |
| Grid icon mode: cell = thumb+8 × thumb+36, word-wrapped name (no elide), hover/selection paint | `mw.cpp:1756-1789`; `fm_item_delegate.cpp:215-239`; `asset_item_delegate.cpp:66-93` | grid cells with elided name; one interact target | port | P1 |
| Details list view: sortable columns, 22 px rows, 18 px icons, sequence-grouping proxy | `mw.cpp:1823-1855` | list view unsortable, no columns | port columns | P1 |
| Placeholder icon by suffix for non-previewable files (pdf/csv/doc(x)/xls(x)/txt/log/md/ai/eps/generic) + folder/drive/computer | `fm_item_delegate.cpp:198-213`; `icon_utils.cpp:212-357` | nothing rendered for non-media | port drawn placeholders | P1 |
| Keyboard: Backspace=up, F2=rename, Shift+Del=permanent delete, Ctrl+C/V/Delete/NewFolder, Space=preview | `mw.cpp:2324-2374, 3190-3193` | C/X/V/Z/Y/S/F/Delete/Enter/Num; no F2/Space/Shift+Del | port shortcuts | P1 |
| Context menu (FM): Refresh, Copy, Cut, Paste, Rename, Bulk Rename, Delete, Permanent Delete, Create Folder with Selected, Add Asset Library, Favorites, Open in Explorer, Properties, Open With, Convert Format | `mw.cpp:3172-3201` | **no context menus** | port (biggest discoverability gap) | P1 |
| Drag out of asset views (`DragOnly`, move default) | `mw.cpp:866-876` | none | port | P1 |
| Drag in/out FM views (`DragDrop`, copy default; Shift=move), drop on tree moves, drop on tag assigns | `mw.cpp:1781-1785, 3404, 1025-1027`; `file_manager_pane.cpp:1302-1313` | none | port | P1 |
| Sequence-aware drag with own mime + count badge; external OS drags (Windows OLE / virtual files) | `fm_views_ex.cpp:17-100`; `drag_utils.cpp:19-37`; `virtual_drag.cpp:130-274` | none | port internal first, platform drag per OS | P1/P5 |

## Preview panel — presentation, not text

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Panel is a **stack of preview kinds**: image (zoom/pan), text/docx, CSV/XLSX table, PDF, SVG, alpha toggle | `mw.cpp:1877-1946` | thumbnail + labels ("worst thing visually") | port kinds progressively | P2 |
| Metadata "File Info": name, path, size, type, dimensions, video codec, audio codec, bitrate, timecode, camera, shot, created/modified, permissions | `mw.cpp:2212-2284, 1048-1126` | status line only | replace with engine metadata (see below) | P2 |
| Star rating widget (click same clears, hover preview) | `star_rating_widget.cpp:13-99`; `mw.cpp:1136-1155` | 5 stars + keys 0-5 | port look, keep wiring | P2 |
| Tags list with Apply/add/context menu; drop targets assign tags | `mw.cpp:974-997, 1021, 6945` | chips + modal add | port list UI | P2 |
| Version History table per asset | `mw.cpp:1136-1155` | none | port with project DBs (P4) | P4 |
| Playback controls: prev/play-pause/next **asset**, position slider, time label, mute button, volume slider | `mw.cpp:2071-2119` | play/pause, stop, seek slider, vol/mute/loop checkboxes | port prev/next asset | P2 |
| Sequence playback (frames on a timer) | `mw.cpp:2121-2129` | engine handles sequences | replace (engine) | P2 |
| Engine metadata covers most File Info fields: width/height, video_codec, bit_rate, timecode, frame_rate/count, audio streams (codec/rate/channels), color/alpha/pixel format, EXIF + text chunks, sequence range | `crates/media/src/metadata.rs` (`MediaMetadata`) | unused by UI | replace with engine | P2 |
| Collections: **no Qt UI exists** — tags are the only grouping besides folders/favorites | grep over `native/qt6/src` | chips UI (engine extra) | keep as extra, not parity work | - |

## Fullscreen review overlay (Space)

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Separate full-window preview: video/sequence/image/text/PDF/AI/SVG; "Preview not available" placeholder | `preview_overlay.cpp:205, 835, 943, 1027, 2194, 2986`; `:890-926` | none (only fullscreen review in plan M2) | port | P3 |
| Controls: alpha-only, loop mode Loop/Once/Ping-Pong, rate 0.25×-4×, prev/play/next frame, mute+volume, side nav ◀ ▶ asset, cached-frame position slider, timecode+fps | `preview_overlay.cpp:332-722, 2757` | transport in panel only | port | P3 |
| Shortcuts: Esc/Space close, ←/→ asset, Ctrl+←/→ and `,`/`.` frame step, J/K/L, ↑/↓ page | `preview_overlay.cpp:1706-1835` | none | port | P3 |
| Zoom: wheel ×1.15/0.85, right-click reset, clamp 0.1-10, double-click fit | `preview_overlay.cpp:1877-1942`; `image_preview_overlay.cpp:210-244` | none | port | P3 |
| Annotation toolbar: select/pen/text/rect/ellipse/arrow, color, width 1-20, undo/redo, clear, save frame (Ctrl+S), save all `{name}_annotation_{frame}.png`; per-frame annotations + timeline marks; JSON per frame | `preview_overlay.cpp:3555-3689, 3994-4080, 1262, 4022`; `annotation_layer.cpp:31-36`; `annotation_items.cpp:30-103` | none | port | P3 |
| Image-only overlay variant with same annotation set (A toggles) | `image_preview_overlay.cpp:45-61, 307-420` | none | merge into one overlay | P3 |

## Grid scrub (what the engine already gives us)

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Ctrl+mouse-move scrubs hovered thumbnail; Ctrl+wheel steps position; 6 px progress strip on the thumb | `grid_scrub.cpp:128-176, 20-63, 459-467` | hover scrubs by pointer x (24 slots); **Ctrl = 200-slot fine division** (`app.rs` SCRUB_SLOTS_FINE) | replace (engine lane + registry equivalent) | done, P1 polish |
| Scrubbable kinds: mp4/mov/avi/mkv/webm/m4v/mxf always; sequences only when grouping on | `grid_scrub.cpp:96-116` | Video/Sequence kinds | port rule (grouping gate) | P1 |
| Frames drawn by the item delegate from a registry, not a widget | `scrub_frame_registry.cpp:9-30, 89-103` | `Pane::scrub_frames` + cell paint | replace | done |

## Data, projects, import, search, bulk rename

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| SQLite schema: `virtual_folders`, `assets`, `tags`, `asset_tags`, `project_folders`, `asset_versions`; FKs on; version PRAGMA | `db.cpp:52-184` | `Library` in rusqlite (`crates/library`) | replace (engine owns schema) | P4 |
| Asset ops: rating, delete, path update, tag rename/delete/**merge**/assign, folder move, checksum jobs | `db.cpp:244-760, 871` | tags create/rename/delete/assign; ratings; relink/forget — **no merge, no checksums** | port the gaps | P4 |
| Change notifications (assets/folders/tags/versions) driving open views | `db.cpp:473-477` | none (views refresh on rescan) | port (event → view refresh) | P4 |
| Database opened and operated **off the main thread** with a job queue | `database_worker.cpp:10-210` | app calls `Library` synchronously from the UI thread | port worker (UI stall risk today) | P4 |
| Health check: orphans, missing files, fragmentation, integrity, index check with progress | `database_health_agent.cpp:19-46` | none | port | P4 |
| Stats: DB size, asset/folder/tag counts, page size | `database_health_agent.cpp:48-88` | none | port | P4 |
| Maintenance: VACUUM, REINDEX, fix orphans, mark missing files | `database_health_agent.cpp:279-313, 354` | none | port | P4 |
| "Database Health Check" dialog: stats group, results tree (Category/Status/Details), Run + progress, Optimize / Rebuild Indexes / Fix Orphaned Records / Check Missing Files | `database_health_dialog.cpp:24-120` | no DB surface at all | port | P4 |
| Project DB per project: name + watch path, own virtual folders/assets (version group key, sequence columns) | `project_db.cpp:35-99` | none | port | P4 |
| Create/rename/delete project, set watch path, resync folders | `project_db.cpp:289-412, 847` | none | port | P4 |
| Project notifications: add / acknowledge / ack-all / unread count (watcher feeds it) | `project_db.cpp:521-602`; `project_manager_watcher.cpp:340-360` | none | port | P4 |
| Projects list columns: name, watch path, created, asset count, unread | `projects_model.cpp:36-49` | none | port | P4 |
| Project asset browser: subfolders + assets; sequence pattern/frames/gaps/version, version group/primary/list | `project_assets_model.cpp:47-135, 277-340` | none | port | P4 |
| Filters there: search text, type filter Images/Videos/Project Files, show-all-versions toggle | `project_assets_model.cpp:177-191, 452-531` | `Library::query/filter_entries(LibraryQuery)` unused | replace with engine query | P4 |
| Virtual folder tree: create/rename/delete/move, drag folders (`x-kasset-folder-ids`), move-vs-copy drop, full path | `virtual_folders.cpp:148-214` | none | port | P4 |
| Watchers: recursive scan with 2 s debounce, new subdirs attached, spurious-change skip, new files → asset + notification, removals surfaced | `project_manager_watcher.cpp:14-23, 97-105, 259-363` | none | port (notify crate already in deps) | P4 |
| Version detection: `_v001` / `_001` on `.aep/.aepx/.nk` → base name, version, group key | `project_version_detector.cpp:8-70` | none | port | P4 |
| Import: media extension whitelist, folder walk in one transaction, sequence detection fast-path, plain-file metadata fast path | `importer.cpp:32-52, 53-130, 146-200, 213-346` | engine scan exists; no import flow | port flow | P4 |
| Import into Asset Library **or** Project DB, threaded, cancellable, progress + current-file signals | `import_controller.cpp:236-286, 484-574`; `project_import_worker.cpp:41-207` | none | port | P4 |
| "Importing Assets" dialog: title, current file, progress, Close | `import_progress_dialog.cpp:11-54` | none | port | P4 |
| Search backend is voidtools **Everything64.dll IPC, Windows-only**; on other platforms the dialog disables search | `everything_search.cpp:146-149, 159-193`; `everything_search_dialog.cpp:29-36` | plan M7: per-OS index + fuzzy ranking | replace (per-OS index beats a Windows-only DLL) | P5 |
| Search dialog: query field, mode (Asset Manager / File Manager), filter combo All/Images/Videos/Audio/Documents, Match Case, results Name/Directory/Size/Modified/Type + **Imported / Not Imported** status column, sort, double-click, Import Selected (skips imported), Open Location, Select Files, "Found N result(s)", cap 10 000 | `everything_search_dialog.cpp:20-36, 48-76, 80-117, 187-201, 244-246, 303-340` | none | port dialog shape over OS index | P5 |
| Bulk rename: pattern with `{###}`/`{original}`/`{date}` tokens, start number + padding, **Update Database** and **Rename Files on Disk** toggles, preview table Original/New/Status with conflict detection, Apply/Cancel | `bulk_rename_dialog.cpp:48-164, 227-242, 374` | none (`fileops::rename` renames one) | port dialog; wire to fileops + Library relink | P5 |
| Sequence grouping in views: frames hidden, display `base.[start-end].ext`, folders-first, toggleable, badges | `sequence_grouping_proxy_model.cpp:27-135`; asset/project variants `:96-149`, `:91-170` | engine groups sequences (`crates/core/src/sequence.rs`); display form + badges missing | replace with engine + port display/badges | P1/P4 |
| Thumbnail generation dialog + worker (video scrub at 0/25/50/75/100 %) | `thumbnail_generator_dialog.cpp:22-185`; `thumbnail_generator_worker.cpp:11-60, 183-318` | engine caches on demand | **drop** (rule 2) | - |

## Settings dialog — Qt keeps very little in it

Qt's settings dialog tabs: **General** (theme), **Cache & Database** (live-preview cache, thumbnail cache, sequence
cache, DB), **View** (thumb size, extensions, sequence badges), **Shortcuts**, **External Apps** (After Effects /
Nuke paths), **About** (`settings_dialog.cpp:66-86, 97-331, 342-365, 375-462, 487-561, 571-615`).

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Dual pane, view mode, thumb size, collapse sequences live on the **toolbar**, not in settings | `mw.cpp:1636-1672` | all four are Settings checkboxes/choices | **move to toolbar** (user complaint) | P1 |
| Theme Dark/Light persisted, full palette + stylesheet | `theme_manager.cpp:21-265` | theme setting exists | port roles; egui style equivalents | P6 |
| Image memory limit with clamp 512-262144 MB and a message pointing at Settings > Cache & Database | `image_memory_limits.cpp:14-19, 46-75` | `ram_preview_mb` setting, no surface | port | P6 |
| Log capture (errors/fatals) + viewer with level filter and Clear | `log_manager.cpp:48-87`; `log_viewer_widget.cpp:14-116` | none | port | P6 |
| User Guide dialog (embedded markdown; F1) | `user_guide_dialog.cpp:18-228` | none | port | P6 |
| Portable data root (`<appdir>/data` if writable, else per-user; AppImage always per-user) | `runtime_paths.cpp:69-106` | app dirs only | port | P6 |

## Phase order and acceptance

Each phase is verified by running the real app first, then adding the smallest failing regression check
(`egui_kittest` for interaction), then passing the review gate — same rules as M1 slices.

| Phase | Scope | Acceptance (user-visible) |
| --- | --- | --- |
| **P1** Browser chrome + views | tabs shell, FM page toolbar, path bar, favorites list, folder tree with drive roots, grid/list toggle + thumb slider on toolbar, placeholder icons, multi-select, context menus, drag/drop internal | "I can go anywhere without typing: tree drives, favorites, Back/Up; I see what a file is before opening it; right-click does the operations; two clips selected and pasted moves both." |
| **P2** Preview panel presentation | preview kind stack + metadata fields from engine + star/tags surfaces + prev/next asset | "The panel shows the thing, not words about it, and playing works from it." |
| **P3** Fullscreen review overlay | Space overlay, shortcuts, zoom, loop/rate, annotations with undo and save | "Space to review a clip, annotate, save the marked frame." |
| **P4** Library/project surfaces | tag+rating filtering (engine query), version history, project DBs, virtual folders, import flow, database health dialog | "My library filters by tag and rating; projects open without me touching files." |
| **P5** Convert + providers + search + platform drag | convert dialog, Office/PDF preview, OS-index search, external drags per OS | "I convert a clip to ProRes and see its PDF; searching the disk is instant enough." |
| **P6** Settings split + system surfaces | settings tabs (General/Cache & Database/View/Shortcuts/External Apps/About), theme roles, logs, user guide, data root | "Settings holds preferences, not buttons I use all day." |

## Explicit drops / demotions (rule 2 applied)

- Thumbnail pre-generation dialog + worker + prefetch/refresh menu → **drop** (engine cache does it).
- Breadcrumb navigation → **replace** with Back/Up + editable path bar (Qt has no breadcrumb).
- Collections chips UI → engine extra, not parity; keep only if it earns its place.
- Per-pane preview panel sharing one panel across panes → Qt gives each pane its own; adopt per-pane panels in P1.
