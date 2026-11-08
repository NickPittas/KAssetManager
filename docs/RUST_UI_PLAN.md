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

Legend: `mw.cpp` = `native/qt6/src/mainwindow.cpp`; other Qt paths are relative to `native/qt6/src/`.

Every row was checked by three bounded fidelity reviewers against the Qt source and against the Rust app, and their
counter-evidence was re-verified by the author before folding (verdicts were DRIFT; ~35 corrections applied).
The corrections that changed plan structure are marked **(review)** in the affected rows.

## Top-level structure — the app is three pages, not one

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Tabbed pages: **File Manager**, **Asset Manager**, **Project Manager** | `mainTabs = QTabWidget`, `mw.cpp:1223/1226/1234` | one browser page | port (tabs) | P1 |
| Workspace restoration, not just last tab: splitters, per-pane view mode / thumbnail size / group sequences /
 hide folders / preview visibility / current path, plus column widths | `mw.cpp:1357, 2515-2559`;
 `file_manager_pane.cpp:1184-1234` | settings file only; panes reset | port (persist per-pane state) | P1 |
| Page toolbars + grid/list stacks; **(review)** the secondary pane's own toolbar is hidden at creation and one
 shared File Manager toolbar retargets the active pane | `mw.cpp:798, 1327, 1735, 2466`;
 `3721-3722`, `syncFmToolbarFromActivePane` `3980-3998` | Grid/List toggle already in the global app bar
 (`app.rs:1553-1567`); one view-mode state | port per-page toolbars; shared toolbar targeting active pane | P1 |
| Auto light-refresh when the directory changes, 1200 ms debounce | `mw.cpp:355-361` | rescan on navigate only | port (watcher + debounce) | P1 |
| Status bar with progress + transient messages | `mw.cpp:345-352, 2900, 3493` | status line only | port | P1 |

**Consequence:** what the Rust app has today is one page (roughly Qt's File Manager page) without its chrome. The
Asset Manager page (library/tag/rating backed) and Project Manager page are still un-built surfaces.

## File Manager page — chrome and navigation

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Back / Up buttons, **no Forward**, no breadcrumb | `mw.cpp:678-693` (FM `1601-1613`) | breadcrumb + explicit Up button
 (`app.rs:1183-1194`); **no Back** | replace (Back/Up + path bar) | P1 |
| Editable path bar, Enter navigates | `mw.cpp:1711-1732` | breadcrumb only | port | P1 |
| New Folder / Copy / Cut / Paste / Delete / Rename / Add-to-Library as toolbar buttons | `mw.cpp:1618-1634` |
 **(review)** Copy/Cut/Paste/Delete/Rename are keyboard-only; **New Folder and Add-to-Library do not exist at all**;
 the Settings window holds none of them (`app.rs:1576-1655`) | add the two missing ops, then toolbar buttons | P1 |
| Grid/List view toggle button | `mw.cpp:1636-1642, 2466` | already a global app-bar button **and** a Settings radio pair | keep on the page toolbar; drop from Settings | P1 |
| Thumbnail size slider (64-320 px FM; 100-400 Asset); **(review)** Qt exposes it in **both** the View tab and the toolbar | `mw.cpp:1645-1652, 716-730`; `settings_dialog.cpp:345` | 4 discrete sizes, Settings only | port slider on toolbar; keep or drop the setting, not both | P1 |
| Group sequences / Hide folders toggles | `mw.cpp:1656-1672` | `collapse_sequences` in Settings; no hide-folders | move + add | P1 |
| Search entry point ("Search entire disk") | `mw.cpp:1675-1681` | none | port (backend is M7) | P5 |
| Preview panel show/hide toggle | `mw.cpp:1683-1691` | always shown | port | P1 |
| Dual-pane toggle creating a **full second pane** (own preview panel and scroll state; **(review)** its own toolbar is
 created but hidden, controls are shared and target the active pane) | `mw.cpp:1693-1698, 3682-3760`;
 `file_manager_pane.cpp:144`; `3721-3722` | second pane shares app chrome; flag in Settings | move toggle to toolbar; per-pane preview + state | P1 |
| Sync-navigation toggle (only when dual-pane) | `mw.cpp:1702-1708` | none | port | P1 |
| Favorites list, double-click opens, add/remove via context menu, persisted | `mw.cpp:1504-1526, 3073-3127, 3314` | `Settings::favorites` field exists, **no UI** | port list UI | P1 |
| Folder tree with real roots: "Computer" → `QDir::drives()`, lazy async expansion, drive icons | `everything_folder_model.cpp:46-50, 573-620, 297-300`; `mw.cpp:1408, 1530-1568` | subfolders of current dir only; no roots | port tree (roots + lazy) | P1 |
| Refresh (F5) + context-menu Refresh | `mw.cpp:2374, 3184` | **(review)** rescan on navigate; **no F5 binding** in the shortcut dispatcher (`app.rs:1685-1738`) | add F5 + menu item | P1 |

## Views, icons, selection

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Multi-select in the browser views (`ExtendedSelection`); **(review)** FM tree and version table are `SingleSelection` | `mw.cpp:814, 834, 1765, 1832, 640`; `1551`, `1167` | **single** selection (`selected: Option<PathBuf>`) | port (blocking for ops at scale) | P1 |
| Grid icon mode differs per page: FM grid wraps names over two lines (no elide); **Asset** grid elides and uses a
 tighter name rect | `mw.cpp:1756-1789`; `fm_item_delegate.cpp:215-239`; `asset_item_delegate.cpp:66-93, 242-262` | one grid renderer, elided name; one interact target | port both delegate behaviours | P1 |
| Details list view: sortable columns, 22 px rows, 18 px icons, sequence-grouping proxy | `mw.cpp:1823-1855` | list view unsortable, no columns | port columns | P1 |
| Placeholder icon by suffix for non-previewable files (pdf/csv/doc(x)/xls(x)/txt/log/md/ai/eps/generic) + folder/drive/computer | `fm_item_delegate.cpp:198-213`; `icon_utils.cpp:212-357` | nothing rendered for non-media | port drawn placeholders | P1 |
| Keyboard: Backspace=up, F2=rename, Shift+Del=permanent delete, Ctrl+C/V/Delete/NewFolder, Space=preview | `mw.cpp:2324-2374, 3190-3193` | C/X/V/Z/Y/S/F/Delete/Enter/Num **+ F2 rename**;
 no Backspace-up, Space preview, Shift+Del, Ctrl+Shift+N (`app.rs:1685-1738`) | port the missing shortcuts | P1 |
| Context menu (FM): Refresh, Copy, Cut, Paste, Rename, Bulk Rename, Delete, Permanent Delete, Create Folder with Selected, Add Asset Library, Favorites, Open in Explorer, Properties, Open With, Convert Format | `mw.cpp:3172-3201` | **no context menus** | port (biggest discoverability gap) | P1 |
| **(review)** A separate **folder-tree** context menu: New Folder, Create Folder with Selected Files, Add Asset
 Library, **Import Project Manager**, Add Favorites, plus move/copy-on-drop | `mw.cpp:3307-3315, 3408-3413` | no tree at all | port with the tree | P1 |
| Drag out of asset views (`DragOnly`, move default) | `mw.cpp:866-876` | none | port | P1 |
| Drag in/out FM views (`DragDrop`, **copy default; Shift=move**), drop on tree, drop on tag assigns | `mw.cpp:1781-1785`;
 `9503-9507` (drop action + queued op); `1025-1027`; `file_manager_pane.cpp:1302-1313` — `:3404` is the
 *context-menu* move/copy operation, not a drop | none | port | P1 |
| Sequence-aware drag with own mime + count badge; external OS drags (Windows OLE / virtual files) | `fm_views_ex.cpp:17-100`; `drag_utils.cpp:19-37`; `virtual_drag.cpp:130-274` | none | port internal first, platform drag per OS | P1/P5 |

## Preview panel — presentation, not text

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Panel is a **stack of preview kinds**: image (zoom/pan), text/docx, CSV/XLSX table, PDF (with prev/next page and a
 `--/--` page label), SVG, alpha toggle | `mw.cpp:1877-1946`; `1970-2055` | **(review)** the panel already draws **decoded playback frames** plus thumbnail and labels
 (`app.rs:1404-1427`) — what is missing is the non-media kinds, not all visuals | port kinds progressively | P2 |
| Metadata "File Info": name, path, size, type, dimensions, video codec, audio codec, bitrate, timecode, camera, lens, reel/scene/take, created/modified, permissions | `mw.cpp:2212-2284, 1048-1126, 8228-8238, 11244-11262` | status line only | replace for media fields; **port** the fields the engine lacks (see below) | P2 |
| Star rating widget (click same clears, hover preview) | `star_rating_widget.cpp:13-99`; `mw.cpp:1136-1155` | 5 stars + keys 0-5 | port look, keep wiring | P2 |
| Tags list with Apply/add/context menu; drop targets assign tags — **(review)** the interactive list lives in the Asset
 Manager **filters panel** (`tagsListView`); the preview info block shows only a tags **label** (`infoTags`) | `mw.cpp:974-997, 1017-1029, 1143-1145, 6945` | chips + modal add | port list in the filters surface, label in info | P2 |
| Version History table per asset + **Revert to Selected** (optional backup, overwrite confirmation) | table built `mw.cpp:1161+`; revert `10397-10419` | none | port with project DBs | P4 |
| **(review)** Library-level **version snapshots** and revert-with-backup are a separate mechanism from filename
 `_v001` version grouping | `db.cpp:824-901` | catalog journal/snapshots exist at the storage layer | map snapshots onto catalog journal | P4 |
| Playback controls in the panel: prev/play-pause/next step **sequence frames** (`fmPrevFrameBtn`, tooltip "Previous
 Frame", `stepFmSequence`), position slider, time label, mute, volume; **(review)** asset-to-asset nav is the overlay's
 ◀ ▶, not these buttons | `mw.cpp:2071-2119`; `preview_overlay.cpp:699-722` | play/pause, stop, seek slider, vol/mute/loop checkboxes | port frame-step buttons in panel; asset nav goes to the overlay | P2 |
| Sequence playback (frames on a timer) | `mw.cpp:2121-2129` | **(review)** the engine can open sequences, but the app only ever calls
 `open()` — never `open_sequence()` (`app.rs:392-408`; `crates/media/src/open.rs:191`), so grouped playback is unwired here | replace (engine) **plus wire `open_sequence`** | P2 |
| Engine metadata covers the media fields: width/height, video_codec, bit_rate, timecode, frame_rate/count, audio
 streams (codec/rate/channels), color/alpha/pixel format, EXIF + text chunks, sequence range. **(review)** It has **no**
 camera/lens/reel/scene/take, no created date (modified only) and no permissions; EXIF is image-only |
 `crates/media/src/metadata.rs:39-65, 149-167` | unused by UI | replace for media fields; port the rest from stat/EXIF | P2 |
| Collections: **no Qt UI exists** — tags are the only grouping besides folders/favorites | grep over `native/qt6/src` | chips UI (engine extra) | keep as extra, not parity work | - |
| **(review)** Preview kinds still un-inventoried by this plan: legacy `.doc`, audio/video presentation in the panel,
 embedded-PDF AI; and Office previews (notably XLSX tables) in the overlay | `mw.cpp:10837-10982`; `preview_overlay.cpp:852-872, 2951-2982` | none | port with providers | P5 |

## Fullscreen review overlay (Space)

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Separate full-window preview: video/sequence/image/text/PDF/AI/SVG; "Preview not available" placeholder | `preview_overlay.cpp:205, 835, 943, 1027, 2194, 2986`; `:890-926` | none (only fullscreen review in plan M2) | port | P3 |
| Controls: alpha-only, loop mode Loop/Once/Ping-Pong, rate 0.25×-4×, prev/play/next frame, mute+volume, side nav ◀ ▶ asset, cached-frame position slider, timecode+fps | `preview_overlay.cpp:332-722, 2757` | transport in panel only | port | P3 |
| Shortcuts: Esc/Space close, ←/→ asset, Ctrl+←/→ and `,`/`.` frame step, J/K/L, ↑/↓ page. **(review)** Ctrl+S (save)
 and A (annotate) are advertised in tooltips but have **no key bindings** — only button clicks | `preview_overlay.cpp:1703-1841, 3679-3680`;
 `image_preview_overlay.cpp:74-75, 228-245` | none | port the real shortcuts; give Ctrl+S/A actual bindings (improvement) | P3 |
| Zoom: wheel ×1.15/0.85, right-click reset; **(review)** the 0.1-10 clamp applies only to the video `zoomImage()` path —
 the still-image overlay is unclamped and its double-click toggles fit vs **1:1** | `preview_overlay.cpp:1877-1942, 2091-2097`;
 `image_preview_overlay.cpp:205-225, 277-282` | none | port; clamp uniformly (improvement) | P3 |
| Annotation toolbar: select/pen/text/rect/ellipse/arrow, color, width 1-20, undo/redo, clear, save frame (Ctrl+S), save all `{name}_annotation_{frame}.png`; per-frame annotations + timeline marks; JSON per frame | `preview_overlay.cpp:3555-3689, 3994-4080, 1262, 4022`; `annotation_layer.cpp:31-36`; `annotation_items.cpp:30-103` | none | port | P3 |
| Image-only overlay variant with same annotation set (A toggles) | `image_preview_overlay.cpp:45-61, 307-420` | none | merge into one overlay | P3 |

## Grid scrub (what the engine already gives us)

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| Ctrl+mouse-move scrubs hovered thumbnail; Ctrl+wheel steps position; progress strip on the thumb. **(review)** corrected
 evidence ranges for gesture handling, frame response and strip geometry | `grid_scrub.cpp:162-179, 191-251, 418-422` (was mis-cited) | hover scrubs by pointer x (24 slots); **Ctrl = 200-slot fine division** (`app.rs` SCRUB_SLOTS_FINE) | replace (engine lane + registry equivalent) | done, P1 polish |
| Scrubbable kinds: video containers always; sequences only when grouping is on | `grid_scrub.cpp:191-251` (eligibility) | Video/Sequence kinds, no grouping gate | port rule (grouping gate) | P1 |
| **(review)** While Ctrl-scrubbing is active the controller **locks to the grabbed item**, clamps the pointer inside that
 thumbnail's rect (warping it) and keeps the position until Ctrl is released — it does not follow whichever cell is hovered | `grid_scrub.cpp:260-278, 323-345, 425-452` | hover-follows freely; per-target clock gives up after 5 s | decide one model in the UI phase; keep ours unless lock feels better | P1 polish |
| Frames drawn by the item delegate from a registry, not a widget | `scrub_frame_registry.cpp:9-30, 89-103` | `Pane::scrub_frames` + cell paint | replace | done |

## Data, projects, import, search, bulk rename

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| SQLite schema: `virtual_folders`, `assets`, `tags`, `asset_tags`, `project_folders`, `asset_versions`; FKs on; version PRAGMA | `db.cpp:52-184` | **(review)** not rusqlite-as-store: `Library` is a **lightcraft-catalog**
 `FsStore` + `Journal` with snapshots, and SQLite is only a one-time legacy import source
 (`crates/library/Cargo.toml:14`; `lib.rs:327-345`) | replace: VACUUM/REINDEX become catalog compaction/snapshot checks | P4 |
| Asset ops: rating, delete, path update, tag rename/delete/**merge**/assign, folder move, checksum jobs | `db.cpp:244-760, 871` | tags create/rename/delete/assign; ratings; relink/forget — **no merge, no checksums** | port the gaps | P4 |
| Change notifications (assets/folders/tags/versions) driving open views | `db.cpp:473-477` | none (views refresh on rescan) | port (event → view refresh) | P4 |
| Long DB jobs run on a worker thread; **(review)** the primary DB is initialised **synchronously at startup** and models
 call it directly, so "everything off the main thread" overstates Qt | `database_worker.cpp:10-210`; `main.cpp:146`; `projects_model.cpp:65-80` | app calls `Library` synchronously from the UI loop (claim correct) | worker for long jobs only (import, health check, bulk relink) | P4 |
| Health check: orphans, missing files, fragmentation, integrity, index check with progress | `database_health_agent.cpp:19-46` | none | port | P4 |
| Stats: DB size, asset/folder/tag counts, page size | `database_health_agent.cpp:48-88` | none | port | P4 |
| Maintenance: VACUUM, REINDEX, fix orphans; **(review)** "check missing files" **scans and counts** — no missing status is persisted | `database_health_agent.cpp:279-313, 354-389` | none | port as catalog compaction + integrity scan | P4 |
| "Database Health Check" dialog: stats group, results tree (Category/Status/Details), Run + progress, Optimize / Rebuild Indexes / Fix Orphaned Records / Check Missing Files | `database_health_dialog.cpp:24-120` | no DB surface at all | port | P4 |
| Projects live in **one** `projects.db`, partitioned by `project_id` — not one file per project; each project has its
 own virtual folders/assets (version group key, sequence columns) | `mw.cpp:4111`; `project_db.cpp:39-55` | none | port (single catalog, project scoping) | P4 |
| Create/rename/delete project, set watch path, resync folders | `project_db.cpp:289-412, 847` | none | port | P4 |
| Project notifications: add / acknowledge / ack-all / unread count (watcher feeds it) | `project_db.cpp:521-602`; `project_manager_watcher.cpp:340-360` | none | port | P4 |
| Projects list columns: name, watch path, created, asset count, unread | `projects_model.cpp:36-49` | none | port | P4 |
| Project asset browser: subfolders + assets; sequence pattern/frames/gaps/version, version group/primary/list | `project_assets_model.cpp:47-135, 277-340` | none | port | P4 |
| Filters there: search text, type filter Images/Videos/Project Files, show-all-versions toggle | `project_assets_model.cpp:177-191, 452-531` | **(review)** `LibraryQuery` covers tags / collection / rating **only**
 (`lib.rs:68-72`) — text, type and version filters are gaps to fill, not existing features | extend the engine query, then replace | P4 |
| Virtual folder tree: create/rename/delete/move, drag folders (`x-kasset-folder-ids`), move-vs-copy drop, full path | `virtual_folders.cpp:148-214` | none | port | P4 |
| Watchers: recursive scan with 2 s debounce, new subdirs attached, spurious-change skip, new files → asset + notification, removals surfaced | `project_manager_watcher.cpp:14-23, 97-105, 259-363` | none | port (notify crate already in deps) | P4 |
| Version detection: `_v001` / `_001` on `.aep/.aepx/.nk` → base name, version, group key | `project_version_detector.cpp:8-70` | none | port | P4 |
| Import into the **asset library** filters by a media extension whitelist; **(review)** import into a **project** takes all
 files (a `.aep` is an asset) — the two paths differ | `importer.cpp:32-52, 53-130`; `import_controller.cpp:385-404` | engine scan exists; no import flow | port both rules | P4 |
| Import runs threaded with progress + current-file signals; **(review)** cancellation lives in `ProjectImportWorker`, not in the
 active library-import loop | `import_controller.cpp:257-281, 484-574`; `project_import_worker.cpp:41-43` | none (file ops run through a worker with undo) | port; make both paths cancellable (improvement) | P4 |
| "Importing Assets" dialog: title, current file, progress, Close | `import_progress_dialog.cpp:11-54` | none | port | P4 |
| Search backend is voidtools **Everything64.dll IPC, Windows-only**; on other platforms the dialog disables search | `everything_search.cpp:146-149, 159-193`; `everything_search_dialog.cpp:29-36` | plan M7: per-OS index + fuzzy ranking | replace (per-OS index beats a Windows-only DLL) | P5 |
| Search dialog: query field, mode (Asset Manager / File Manager), filter combo All/Images/Videos/Audio/Documents, Match Case, results Name/Directory/Size/Modified/Type + **Imported / Not Imported** status column, sort, double-click, Import Selected (skips imported), Open Location, Select Files, "Found N result(s)", cap 10 000 | `everything_search_dialog.cpp:20-36, 48-76, 80-117, 187-201, 244-246, 303-340` | none | port dialog shape over OS index | P5 |
| Bulk rename: pattern with `{###}`/`{original}`/`{date}` tokens, start number + padding, **Update Database** and **Rename Files on Disk** toggles, preview table Original/New/Status with conflict detection, Apply/Cancel | `bulk_rename_dialog.cpp:48-164, 227-242, 374` | **(review)** the engine already has it: `RenameRule`, `RenamePlan`, `RenameStatus`,
 `plan_renames()` and `FileOp::BatchRename` execution (`crates/fileops/src/rename.rs:10-150`; `worker.rs:377, 528`) —
 **only the dialog is missing** | build the dialog over the existing engine | P5 |
| Sequence grouping in views: frames hidden, display `base.[start-end].ext`, folders-first, toggleable, badges | `sequence_grouping_proxy_model.cpp:27-135`; asset/project variants `:96-149`, `:91-170` | engine groups sequences (`crates/core/src/sequence.rs`); display form + badges missing | replace with engine + port display/badges | P1/P4 |
| Thumbnail generation dialog + worker (video scrub at 0/25/50/75/100 %) | `thumbnail_generator_dialog.cpp:22-185`; `thumbnail_generator_worker.cpp:11-60, 183-318` | engine caches on demand | **drop** (rule 2) | - |

## Settings dialog — Qt keeps very little in it

Qt's settings dialog tabs: **General** (theme), **Cache & Database** (live-preview cache with entry count and clear,
 thumbnail cache with file count/size, cache directory browse, generated size W×H, clear, sequence cache as
 auto-%-of-RAM vs manual frame budget with an estimated-memory readout, database export / destructive import-replace /
 clear-all with confirmations), **View** (thumb size, extensions, sequence badges), **Shortcuts** (editable Action/Shortcut
table with duplicate rejection, per-row Reset and Reset All), **External Apps** (After Effects / Nuke paths), **About**
(`settings_dialog.cpp:66-86, 97-331, 120-123, 169-175, 215-305, 310-325, 342-365, 375-462, 423-457, 487-561, 571-615,
 634-689, 751-775`).

| Feature | Qt evidence | Rust today | Decision | Phase |
| --- | --- | --- | --- | --- |
| View mode, dual pane, preview visibility, group sequences and hide folders live on the **toolbar**; **(review)** thumbnail
 size is in **both** the toolbar and the View tab | `mw.cpp:1636-1672`; `settings_dialog.cpp:345` | Grid/List already in the app bar; dual pane / thumb size / collapse sequences /
 show-non-media are Settings-only (`app.rs:1576-1655`) | move the day-to-day controls to the toolbar | P1 |
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
| **P1** Browser chrome + views | tabs shell, per-page toolbars targeting the active pane, path bar, favorites list, folder tree with drive roots and its own context menu, view-mode toggle + thumb slider, placeholder icons, multi-select in browser views, FM context menus, internal drag/drop, auto light-refresh, per-pane state restoration | "I can go anywhere without typing: tree drives, favorites, Back/Up; I see what a file is before opening it; right-click does the operations; two clips selected and pasted moves both." |
| **P2** Preview panel presentation | preview kind stack (incl. PDF page nav) + engine metadata plus the ported fields + stars + tag list in the filters surface + sequence frame-step controls and `open_sequence` wiring | "The panel shows the thing, not words about it, and playing works from it." |
| **P3** Fullscreen review overlay | Space overlay, shortcuts, zoom, loop/rate, annotations with undo and save | "Space to review a clip, annotate, save the marked frame." |
| **P4** Library/project surfaces | tag/rating filtering plus the query gaps (text, type, versions), version history and catalog snapshots, `projects.db`-style project scoping, virtual folders, import flows, catalog health/maintenance UI on a worker thread | "My library filters by tag and rating; projects open without me touching files." |
| **P5** Convert + providers + search + platform drag | convert dialog (targets, codecs, scaling, conflict policy, progress/cancel, retry/skip), Office/PDF preview incl. XLSX tables, OS-index search, bulk-rename dialog over the existing engine, external drags per OS | "I convert a clip to ProRes and see its PDF; searching the disk is instant enough." |
| **P6** Settings split + system surfaces | settings tabs (General/Cache & Database/View/Shortcuts/External Apps/About), theme roles, logs, user guide, data root | "Settings holds preferences, not buttons I use all day." |

## Explicit drops / demotions (rule 2 applied)

- Thumbnail pre-generation dialog + worker + prefetch/refresh menu → **drop** (engine cache does it).
- Breadcrumb navigation → **replace** with Back/Up + editable path bar (Qt has no breadcrumb).
- Collections chips UI → engine extra, not parity; keep only if it earns its place.
- Per-pane preview panel sharing one panel across panes → Qt gives each pane its own; adopt per-pane panels in P1.

## Review provenance

Three bounded fidelity reviewers checked this plan row-by-row against the Qt source and the Rust app. All three
returned **DRIFT**; every finding was re-verified by the author against the cited lines before folding, and none was
dismissed (40 corrections applied):

| Reviewer | Scope | Findings |
| --- | --- | --- |
| `plan-chrome` | top-level structure, FM chrome/navigation, views/icons/selection | 12 |
| `plan-preview` | preview panel, fullscreen overlay, grid scrub | 12 |
| `plan-data` | data/projects/import/search/bulk rename, settings dialog, explicit drops | 12 |

What the review established about the plan's central question — *is this refactoring the original application?* —
the answer is now yes with corrections: the plan describes Qt's actual behaviour (shared toolbar targeting the active
pane, frame-stepping rather than asset-stepping in the FM panel, copy-default/Shift-move drops, tags list in the
filters panel, single `projects.db`, thumbnail size present in both places), and its "Rust today" column no longer
overstates or understates what exists (F2 already wired, no F5, Grid/List already in the app bar, panel already draws
decoded frames, `open_sequence` never called, batch rename already in `fileops`, catalog not rusqlite).

Residual unknowns recorded by the reviewers: runtime PDF/OIIO availability, Windows IPC and converter execution, and
platform drag behaviour were read from source but not executed.
