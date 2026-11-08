# Rust Port Plan (branch `rust-refactor`)

Decisions taken: full app port to Rust + egui; big-bang swap of the app with Qt kept building as a
reference; engine crates forked into this repo; first shippable version must include annotations +
fullscreen review overlay, project DB + virtual folders + DB health, media convert + Office/PDF
preview, and search. The Rust app ships on **Windows, Linux and macOS** (the C++ original shipped
Windows + Linux only). Memory safety is a primary motivation, not theoretical: the original app
segfaulted in many places with no clear trace.

## Evidence this plan is based on

| Fact | Source |
|---|---|
| C++ app to port: ~51k LOC under `native/qt6/src` (+ player_lab, tests) | `wc -l` over `src/`, `player_lab/`, `tests/` |
| `mainwindow.cpp` 12,089; `preview_overlay.cpp` 4,345 + 847 hdr; `ffmpeg_mov_player.cpp` 1,888 | same |
| Playback today: FFmpeg via pkg-config + tlRender; scrubbing `grid_scrub.cpp` 524 + `live_preview_manager.cpp` 991 | `native/qt6/CMakeLists.txt:90`, codemap.md |
| GalleryBrowser is a Rust workspace of **7 packages** (6 library crates + 1 app), ~26k LOC; player is UI-free (`tick(Instant) -> Option<Arc<VideoFrame>>`) | `crates/player/src/lib.rs` |
| Today's CI never builds the Qt application: both configure steps pass `-DBUILD_APP=OFF`, and triggers cover only `main` — "Qt keeps building" is not currently proven by anything | `.github/workflows/ci.yml:5,7,35,68` |
| Decoders are pure-Rust ArtCraft crates, no FFmpeg; coverage claims ProRes, H.264/HEVC, DNxHD/HR, AV1, MXF/MOV | `crates/media/src/lib.rs:1-2` |
| No FFI layer exists in GalleryBrowser (no `extern "C"` anywhere) — irrelevant for a full egui port | grep over `crates/`, `apps/` |
| GalleryBrowser CI builds on ubuntu/macos/windows with `rust-toolchain@stable`; branch history shows it already did a Python→Rust rewrite (`rust-rewrite`) | `.github/workflows/ci.yml`, `git branch -a` |
| No Rust toolchain was installed on this machine; now `cargo 1.99.1` / `rustc 1.99.1` via mise | `mise use -g rust@stable` |
| fzf is a cross-platform fuzzy *filter*; it does not maintain a system-wide filename index, so it cannot replace Everything's index query | voidtools.com/support/everything, junegunn/fzf README |
| `nucleo` / `nucleo-matcher` (helix-editor) is an active Rust library port of fzf's scoring algorithm — in-process fuzzy ranking, no external binary at runtime | github.com/helix-editor/nucleo (core matcher described as complete; used by Helix) |
| GalleryBrowser already ships CI for ubuntu + macos + windows with `rust-toolchain@stable`, so the three-platform bar is proven in the sibling repo | `.github/workflows/ci.yml` matrix |

## Target layout

```
Cargo.toml                 # workspace: crates/*, apps/*
crates/                    # engine crates forked from GalleryBrowser @ recorded rev (VENDORED.md)
  core/ media/ player/ fileops/ library/
apps/kassetmanager/        # new egui app = the ported UI
native/qt6/                # frozen reference build; local-only (decided: no CI job on this branch)
third_party/ffmpeg         # candidate for deletion once M2 passes (media convert may keep ffmpeg CLI)
```

Fork only *our* crates. The ArtCraft crates (`filmcraft-*`, `photocraft-*`, `lightcraft-*`,
`craft-ui`) stay pinned git dependencies — forking third-party is self-inflicted maintenance.

## Milestones (each ends in a runnable check, not a claim)

| # | Deliverable | Gate (how it is proven) |
|---|---|---|
| M0 | Cargo workspace skeleton + vendored engine crates compile here. **No CI change for Qt** (decided: the reference build is local-only); cargo jobs are added, Qt's existing `main`-only CI stays as it is | `cargo build --workspace` and `cargo test --workspace` on ubuntu + macos + windows; `cargo clippy --workspace --all-targets -- -D warnings` and `cargo fmt --check` on ubuntu only (as GB does); pinned ArtCraft revs compile on MSVC and macOS; locally, one `-DBUILD_APP=ON` configure+build of `native/qt6` proves the reference still compiles |
| M1 | egui app shell in a **fresh** crate `apps/kassetmanager`: eframe/wgpu window, menu + status bar, dual-pane browser, folder tree, breadcrumb, settings dialog, theme. Decided: **no GalleryBrowser UI code is copied** — panes, tree, breadcrumb and transport are written here against the five engine crates (their `AppState`/action-dispatcher model is not inherited) | Scripted over a fixture tree with `egui_kittest`: navigate, multi-select, inline rename, trash, new folder, then undo and redo — each asserted on the filesystem (paths exist / gone / restored), not observed by eye. First-frame presented within a declared frame-time budget on this machine (number frozen in the M1 test before it runs). |
| M2 | **Motivation gate** — playback + hover scrub in Rust: one `Player` per pane, transport, `media::Thumbnailer` scrub slots, texture LRU | Named ProRes MOV fixture (path + duration recorded): plays with audio at 1x; at speed != 1 the engine **mutes and skips audio by design** (`crates/player/src/lib.rs:17-20`), so the gate is *video-correct* at 2x, not audio-at-2x; scrub over the fixture does not reopen the container (assert on open-source count); normalized pixel-hash comparison against FFmpeg decode for named frames |
| M3 | Asset catalog in Rust: project DBs, virtual folders, `asset_versions`, DB health agent + dialog, on `rusqlite` | Opens a Qt-produced SQLite fixture and lists frozen expected counts; each health check matches frozen expected findings from the Qt `database_health_agent`; **write** paths exercised too (create virtual folder, add asset version, run one repair) — read-only counts prove nothing about migration ownership |
| M4 | Import workflow + folder watchers: scan → sequence detect → ingest with progress; dedupe, relink | Import a fixture folder containing an EXR sequence (manifest records frame count + naming) → one grouped asset; re-import adds nothing; watcher picks up a dropped file within a declared deadline; moving an ingested file then running relink restores the link. |
| M5 | Fullscreen review overlay + annotation markup (pen / text / rect / arrow) — the two hardest egui surfaces | Draw each of the four annotation types on a frame, save, reopen: geometry matches within a frozen pixel tolerance at three frozen zoom factors; fullscreen entry and exit both restore layout. |
| M6 | Media convert (ffmpeg CLI over `std::process`), Office preview (docx via zip, OLE `.doc`), PDF preview | Convert MOV→MP4 and assert output codec + duration with the same probe used today (not file existence); preview asserts extracted content — known string from a `.docx`, first page of a `.pdf`, and an OLE `.doc`. |
| M7 | Search: one trait, three OS backends, fuzzy ranking in-process (see "Search design" below) | Frozen expected path set and ranking order for named queries over one fixture tree, per OS backend, plus the failure-mode cases below. `nucleo-matcher` assertions run without any OS binary. |

Deferred on purpose (not dropped): drag-out to other OS apps (Win32 virtual-file drag), log dock polish.

### Gate acceptance rule

Every numeric threshold above (frame time, pixel tolerance, zoom factors, watcher deadline, expected
counts and findings) is **declared before the milestone's implementation starts** and then **frozen in a
committed test once the feature is verified**. Nothing in this plan invents a number from here; that keeps
gates falsifiable without pretending today's guesses are requirements.

Order of work per feature (repository rule): implement → **run the real application and verify the
behavior** → only then add the smallest test that would fail if the behavior regressed. A test is a net
under already-verified functionality, never the first artifact written for a feature.

Review gate (repository rule): once a feature is verified and its regression net exists, pass the change
through a review agent before calling it done; findings are fixed or explicitly dismissed with evidence.

UI/UX gate (repository rule): keep finishing backend/engine-facing surfaces while the UI is still
bare-minimum "just works". Before any UI-side work starts, write a plan for how the UI should be shown —
layout, density, affordances, command discoverability — and treat that plan as the spec for the UI phase.

## M0 order of operations (no further decisions needed)

1. Record the GalleryBrowser source SHA, then copy the five crates and their manifests into `crates/`.
2. Carry the workspace `[workspace.dependencies]`, `[workspace.package]` and `[workspace.lints]` blocks so
   the forked crates keep the same lint wall (`unsafe_code = forbid`, `unwrap_used`/`expect_used`/
   `panic`/`unreachable`/`unimplemented` denied).
3. Preserve the ArtCraft git revs exactly as pinned today; do not bump them during the fork.
4. `cargo build` + `cargo test` locally (already proven green against upstream here: 14 test binaries ok).
5. Add the Rust CI jobs with an explicit `rust-refactor` branch trigger — Qt's CI untouched.

`crates/VENDORED.md` records, per crate: source URL + SHA, which paths were copied, the copy mechanism,
license attribution (MIT), and every divergence from upstream (starting with the macOS `~/.Trash` change).

Fixture policy, respecting this repo's rule that generated artifacts stay inside the project folder:
committed fixtures under `tests/fixtures/`, generated outputs in an ignored in-project directory, and any
inherited test that used the system temp dir redirected there (e.g. `crates/fileops/src/worker.rs:690`).

## Search design (M7)

Everything did two jobs at once; the port separates them so no per-OS binary is required for ranking:

| Job | Windows | macOS | Linux |
|---|---|---|---|
| Indexed filename query | Everything SDK / `es` (the existing `everything_search.cpp:13-116` path already loads `Everything64.dll`) | `mdfind` (Spotlight index — closest analog to Everything's always-on index) | `plocate`/`locate` DB, or `fd` when no index is present |
| Fuzzy ranking + type-ahead | `nucleo-matcher` in-process on all three (fzf's scoring algorithm, no fzf binary dependency) | same | same |

Consequence: one `SearchProvider` trait with a per-OS implementation and a fake in tests; the existing
~1.6k LOC of Qt search UI + results model collapses to a text field plus a result list.

Two contract details that must not be discovered late:

- **The index filters, it does not fuzzy-match.** `es`/`mdfind`/`plocate` narrow candidates with their own
  literal/prefix semantics *before* `nucleo-matcher` can rank them, so a typo'd query can lose the match
  before ranking ever sees it. The provider must issue a permissive pattern (prefix/substring), not an
  exact match — otherwise fuzzy ranking is decorative.
- **Failure modes are part of the gate:** index service unavailable or not running, backend command error
  or timeout, stale or volume-excluded index, and missing `fd`/`plocate` binary. Each needs a declared
  user-visible outcome rather than an empty result.
- Fallback rule: if the OS index is absent or cold, degrade to `fd` or a scan over the project's own DB —
  and disclose that the DB fallback only covers **imported** files, so coverage narrows silently.

## What must be resolved before each gate touches code

1. `preview_overlay.cpp` is 4,345 lines in one file. Read it at the start of M5 — it likely holds
   two or three surfaces (filmstrip, transport, annotation host). Sizing M5 from its line count
   alone would be guesswork.
2. egui has no `QGraphicsView`/`QGraphicsScene`. Zoom-pan-rotate (`image_preview_overlay.cpp`) and
   annotations become custom paint + an interaction state machine. Budget for it, do not assume parity.
3. DB compatibility: schema ownership is **three places**, not one — `db.cpp:52-59` (foreign-key
   enforcement and the latest-version constant), `db.cpp:60-186` (base DDL through the index block and
   the `user_version` bump), plus `project_db.cpp:35-102` (`ProjectDB::migrate`: projects, version
   grouping, notifications). M3 starts by pinning all three as fixtures, not by designing a new schema.
4. Decoder coverage against *this* library, not the claim in the doc: enumerate the actual codecs in
   use (MXF? BRAW? which audio codecs inside MOV?) and confirm ArtCraft decoders handle them at M2.
5. wgpu on Wayland / Fedora 43 AppImage: the Qt path needed `AA_ForceRasterWidgets` (`main.cpp:59-77`).
   Prove a wgpu surface plus video-frame upload is smooth on this machine early (M1), and check that
   the AppImage for a Rust binary needs no Qt payload at all.
6. Fork hygiene: record the source rev in `crates/VENDORED.md` so upstream GalleryBrowser fixes stay
   cherry-pickable instead of becoming a merge argument.

## Platform matrix (Windows + Linux + macOS)

| Concern | Plan |
|---|---|
| Trash undo | Decided: trashing on macOS becomes a **move to `~/.Trash`** so undo works, diverging deliberately from GalleryBrowser's `fileops` (`crates/fileops/src/worker.rs:336-345`, and the restore path at `:648-655`). Tests must cover: name already present in `~/.Trash` (a trash-*destination* collision, not a restore one), an occupied original path when restoring, and redo after undo. |
| Symlinks on Windows | `fileops` handles symlink privilege and type differences on Windows (`crates/fileops/src/fsops.rs:86-101`). The C++ app's behavior here is unspecified — decide during M1 whether copy/move follows or preserves links, then test it. |
| CI | **Linux only** (`ubuntu-latest`): build, test, strict clippy, `fmt`. Windows and macOS compile on their own hardware — Kamino-1 (Windows PC) and the MacBook — by pulling this branch over SSH (Tailscale IPs to come) and building there; a hosted 3-OS matrix duplicated that per push, cost three setups, and cancelled its own predecessors. **No Qt reference job on this branch** — decided local-only; today's CI configures `-DBUILD_APP=OFF` and triggers on `main` only (`.github/workflows/ci.yml:5,7,35,68`). |
| Packaging | Linux: AppImage for a single Rust binary — no Qt payload, so `scripts/build-linux-appimage.sh` and the `qtpaths` plugin harvesting mostly disappear. Windows: portable zip or MSI; macOS: `.app` + dmg. |
| Audio | cpal is already in the player crate and is cross-platform (WASAPI/CoreAudio/ALSA); M2's audio gate must be checked on all three, not just Linux. |
| GPU surface | wgpu replaces Qt's raster/GL path; Wayland needs proof on Fedora 43 — **slice 1 has it** (window maps, status bar renders, empty stderr). Windows/macOS get their first-run smoke check on Kamino-1 and the MacBook, which is also where `eframe`/`winit` compiling there gets answered for the first time. |
| Decoder supply chain | ArtCraft crates are pure Rust, so no FFmpeg DLLs to ship; the pinned revs' MSVC and macOS builds were already proven once in CI (run 42489707491, all three runners green). Later platform checks move to Kamino-1 and the MacBook. |

## Crash triage (why memory safety is a primary motivation)

The original app segfaulted in many places with no clear trace on trigger. Two consequences for this plan:

1. The Qt reference build is **not a reliable behavioral oracle** — where C++ crashes or reads freed
   memory, "match the reference" is meaningless. M1–M7 gates therefore compare against declared
   expectations and fixtures, not against C++ runtime behavior on the same input.
2. Rust removes the class of bug rather than tracing it: `unsafe_code = forbid` plus `unwrap_used`,
   `expect_used`, `panic`, `unreachable`, `unimplemented` denied at workspace level (GalleryBrowser's
   `[workspace.lints]`) means a crash needs an explicit, reviewable `unsafe` or `unwrap`.
3. ASan/UBSan stays where it is: the C++ jobs run on `main` and keep characterizing crashes users
   report against shipped builds. On `rust-refactor` you run them **locally** when a reference behavior
   matters — that is the cost of the local-only decision, and it must be explicit rather than assumed.

## Rough size estimate

~14.6k LOC of engine crates arrive already written (src only: core 1,788 · media 3,732 · player 4,954 ·
fileops 2,539 · library 1,621). The port itself is UI + app logic: call it 18–24k LOC Rust against
~51.2k LOC C++ under `src` (58.7k including player_lab and tests). M0–M2 are where the stated
motivations (MOV/ProRes reliability, no bundled FFmpeg, memory safety) are actually banked; M3–M7 are
the bulk of the code.

Caveat on "memory safety removes the class of bug": workspace lints forbid `unsafe` and deny
`unwrap`/`panic`, but indexing panics (`slice[i]`), allocation failure, and faults inside native
dependencies (wgpu/cpal) remain possible. Rust shrinks the crash surface; it does not zero it.

## Decisions resolved (before M0)

1. **Speed-change audio: accepted as-is.** The engine mutes and skips audio at speed != 1 (no
   pitch-correct stretch, `crates/player/src/lib.rs:17-20`), so v1 is silent off 1x. This is a visible
   regression against today's `QMediaPlayer` path — worth one line in release notes, not code.
2. **macOS trash undo: fixed.** Trashing becomes a move to `~/.Trash` on macOS so restore works. First
   and only planned divergence from the forked GalleryBrowser crates; record it in `crates/VENDORED.md`.
3. **Reference build: local-only.** No Qt CI job on this branch. Consequence to remember: nothing proves
   the C++ app still compiles on push, so run `-DBUILD_APP=ON` locally before trusting a reference behavior.

4. **UI starting point: fresh app crate.** `apps/kassetmanager` is written from scratch against the five
   engine crates; GalleryBrowser's `ui-egui` stays unreferenced (not vendored, not copied). Cost accepted:
   folder tree, breadcrumb, dual-pane selection and drag/drop are re-implemented (~2.7k LOC) instead of
   inherited. Benefit: no Action-dispatcher model to work against when the asset DB arrives at M3.
5. **Interaction gate mechanism:** `egui_kittest` 0.36.2 as a dev-dependency (same egui series as upstream's
   0.36), headless `Harness` with clicks/key presses and accessibility-based widget queries; snapshots stay
   optional behind its `wgpu`/`snapshot` features. This settles how M1–M7 gates are executed.

### Still open — cross-cutting concerns, assigned to milestones

Each needs one sentence of decision when its milestone starts, not a redesign. Only the first affects M0:

| Concern | Assigned |
|---|---|
| egui interaction/regression test strategy (how a scripted UI test drives focus, drag, keyboard) | **M0** — it decides how every later gate is executed |
| DB write/migration ownership with backup and rollback | before M3 writes |
| FFmpeg CLI runtime supply and licensing on all three platforms | before M6 |
| Font fallback / Unicode text rendering; HiDPI and mixed-monitor coordinates | M1 (first pixels on screen) |
| Keyboard focus, shortcut precedence, clipboard interop | M1 |
| Window/layout persistence (`mainwindow.cpp:1268-1312`) plus `QSettings` data migration from the Qt app | M1 layout, migration before first real DB write (M3) |
| Shipped-build logging and panic diagnostics (distinct from the log dock) | M1 for logging, before shipping for crash reports |

## M0 result (recorded evidence)

Run on this Fedora machine, `cargo`/`rustc` 1.99.1 via mise, `TMPDIR` pointed inside the repo
(`build-rust-tmp/`) so no test artifact landed outside the project folder.

| Check | Result |
|---|---|
| Vendoring | Five crates copied from GalleryBrowser `55a44836d44dd5db5f079c42ce86f712f7360309`; 41 files; `diff -rq` against the source checkout clean (byte-identical). After M0's lint fix, 14 of those files carry an attribute-only test-scope allow — no logic diverged |
| Workspace | Root `Cargo.toml` is GalleryBrowser's verbatim except `members = ["crates/*"]` (`diff` shows only those two lines); new `Cargo.lock` resolves 275 packages with the three ArtCraft revs identical to upstream |
| `cargo build --workspace --locked` | Finished in 50.5s, **zero warnings** under the workspace lint wall |
| `cargo test --workspace --locked` | **190 passed, 0 failed, 1 ignored** across the workspace's test targets |
| `cargo clippy --workspace --all-targets -D warnings` | Initially **failed** on `expect_used`/`unwrap_used`/`panic` in test code across 4 crates. Fixed by applying the pattern `library` already uses upstream (`crates/library/src/lib.rs:991`) to 14 test scopes; now **clean**, so CI keeps a single strict pass and M1's app binary inherits the same wall |
| Toolchain | `rust-toolchain.toml` pins `1.99.1` + clippy/rustfmt (the snapshot M0 was verified with); `rust-version = "1.95"` remains the declared crate minimum. Verified local resolution still works under mise |
| `cargo fmt --all -- --check` | Clean |
| Qt reference build | Fresh configure of `native/qt6` with `-DBUILD_APP=ON -DBUILD_TESTS=OFF` + build linked `kassetmanagerqt` in 26s. Two environment notes: the pre-existing `build-linux/` cache is unusable here (created from another worktree path), and `-G Ninja` fails because this box exposes `ninja-build`, not `ninja` |
| ~~Not verifiable locally~~ **resolved** | CI run 42489707491 on `7f1eea1`: `build + test` **success** on ubuntu-latest, windows-latest and macos-latest — the pinned ArtCraft revs compile and pass tests on MSVC and macOS |

## M1 progress

**Slice 1 (verified):** `apps/kassetmanager` opens a wgpu window under Wayland on this Fedora box.
Evidence: `hyprctl clients` reported `class io.github.nickpittas.KAssetManager`, `title "KAsset Manager"`,
empty stderr over ~20s of running, and a screenshot (`validation_outputs/m1-slice1.png`) shows the bottom
status bar rendering with its `ping` button and `clicks:` label. The Qt build's `AA_ForceRasterWidgets`
caveat therefore does not apply to the Rust path.
Known rough edge from the same screenshot: Hyprland mapped the window clipped against the right screen
edge — window placement/sizing (and HiDPI scaling) is an M1 item, not a render defect.
After verification, one `egui_kittest` test exists as the regression net for that verified interaction
(`Harness::new_ui_state` + click by label); egui 0.36 API notes learned the hard way: the app trait method
is `fn ui(&mut self, &mut egui::Ui, &mut Frame)` (not `update`), there is no `TopBottomPanel` — use
`egui::Panel::bottom(...)`, and widget queries need `use egui_kittest::kittest::Queryable;`.

**Slice 2 (verified):** browsing a real folder. Running `kassetmanager ~/Pictures` showed the breadcrumb
(`up / home / npittas / Pictures`), a Folders panel with that folder's 9 subfolders, 14 media entries
(including `.mov`, `.mp4`, `.heif`) and the status bar `/home/npittas/Pictures · 14 entries`, with empty
stderr (`validation_outputs/m1-slice2.png`). Data comes from the forked engine: `scan_folder` +
`sort_entries` + `list_subfolders` with `show_hidden=false, collapse_sequences=true, show_all_files=false`.
The window also mapped fully on screen this time, so the earlier right-edge clipping was placement luck,
not a sizing bug — still an M1 item to pin. After that verification one sandbox test was added (entries
listed, folders offered, navigation re-scans).
Still not there: subfolders are a flat list, not a lazy tree; no thumbnails; window size/HiDPI and OS
drag/drop untouched.

**Slice 3 (verified):** preview panel fed by the engine. `gallerybrowser_media::Thumbnailer` (2 decoder
threads, 1 GB disk cache per `Settings` defaults) renders the selected entry at a 1024 px max edge; results
are polled each frame and uploaded as a wgpu texture. Evidence: running `kassetmanager ~/Pictures/Apps.jpg`
showed that file selected in the list, its decoded picture in the Preview panel, status bar
`/home/npittas/Pictures · 14 entries · selected: Apps.jpg`, empty stderr
(`validation_outputs/m1-slice3.png`). The CLI now accepts a file and selects it on start, which is how the
preview path was verified without GUI automation. Two state tests were added after that verification.
egui/epaint 0.36 notes: `TextureHandle` has `size_vec2()`/`id()` (no `width()`/`height()`),
`egui::Panel::right(id)` exists, and images are drawn with `ui.image(egui::load::SizedTexture { id, size })`.
Still not there: playback of video (no `gallerybrowser-player` in the app yet — a click on a `.mov` gives
a first frame only), no scrubbing.

**Slice 4 (verified):** grid view with engine thumbnails. Opening a folder batches one thumbnail request per
media entry at the engine's `thumbnail_size` (128 px default) through the same `Thumbnailer`; results are
polled into wgpu textures and drawn in rows that fit the panel, with the path bar offering a grid/list
toggle (`Settings::view_mode`, Grid is the engine default). Evidence: running `kassetmanager ~/Pictures`
showed real thumbnails for images **and** videos (`demo.mov`, `demo_small.mov`, an `.mp4`, and
`sample1.heif` all decoded), names beneath each cell, `List` toggle in the path bar, status bar intact
(`validation_outputs/m1-slice4.png`). Test net added afterwards: one request outstanding per entry, non-media
files excluded, view mode is app state.
Still not there: no lazy folder tree, no settings dialog (settings are engine defaults), window size/HiDPI
and OS drag/drop untouched.

**Slice 5 (verified):** playback through `gallerybrowser_player::Player`. Double-click (or a file given on
the command line) opens the file with `gallerybrowser_media::open`, loads it in the player with autoplay,
and each frame pumps `tick(Instant::now())` into an RGBA upload; transport sits under the picture —
play/pause, stop, position readout and a seek slider (`Tick::from_seconds_f64`). Evidence: running
`kassetmanager ~/Pictures/demo_small.mov` showed decoded video frames in the preview panel, the button
reading `Pause` (i.e. playing), and the readout at **3.3 / 5.6 s**; a second shot three seconds later had a
different transport crop, so the playhead advances (`validation_outputs/m1-slice5a.png`, `-b.png`). Audio is
live (cpal backend via `Player::new`), which is why M2's audio-at-speed gate still has to be run.
Test net added afterwards: an unopenable file reports an error and loads nothing; a real media file loads.
Still not there: frame upload allocates a new texture per frame, no volume/mute UI, no lazy folder tree,
window size/HiDPI and OS drag/drop untouched.

**Slice 6 (verified):** settings are real. The app loads `Settings` from `AppDirs::settings_file()` at startup
(engine defaults when absent), the scan follows them (`show_hidden`, `show_all_files`, `collapse_sequences`,
`sort_key`/`sort_ascending`) and thumbnail threads come from `thumbnail_threads`. A Settings window — opened
by button or the `S` shortcut — edits theme, view mode, thumbnail size and the three filters; every change
applies at once and is written back through `Settings::save`, failures surface in the status bar.
Evidence: `kassetmanager --theme light ~/Pictures` rendered the whole UI light with empty stderr
(`validation_outputs/m1-slice6-light.png`), and `wtype -k s` on the focused window opened the dialog showing
`Theme: Dark|Light|System`, `View: Grid|List`, `Thumbnails: 64|128|256|512`, unchecked hidden/non-media and
checked collapse, plus Close (`validation_outputs/m1-slice6-settings.png`). Test net added afterwards:
filters come from settings (defaults hide hidden and non-media files; both on lists all three sandbox files).
`System` theme is treated as dark — `ponytail:` until we can ask the OS. Keyboard handling now exists (one
key), which is how the dialog got verified without a mouse daemon: ydotool is useless here (no `ydotoold`),
`wtype` works.
Still not there: no volume/mute UI, no lazy folder tree, window size/HiDPI and OS drag/drop untouched.

**Slice 7 (verified):** the dual-pane browser. State moved into a `Pane` struct (path, entries, subfolders,
selection, preview) held twice by the app; the second pane docks right with a draggable splitter
(`egui::Panel::right("pane2").resizable(true)`) and is shown only when `Settings::dual_pane` says so. Each
pane owns its breadcrumb (horizontally scrollable so narrow panes cannot clip it), subfolder list and
grid/list of engine thumbnails; clicking anything makes that pane active, and the status bar reports the
active pane. Thumbnails, preview textures, the player and settings stay app-global — one decode pipeline,
one playback at a time. Evidence: `kassetmanager --dual-pane ~/Pictures` rendered two independent panes with
their own breadcrumbs, folder lists and thumbnail grids, one-line status bar
`/home/npittas/Pictures · 14 entries · pane 1`, empty stderr
(`validation_outputs/m1-slice7-dual-fixed.png`; the first shot `m1-slice7-dual.png` showed the two layout
defects that were then fixed). Test net added afterwards: `panes_are_independent` (navigating pane 2 leaves
pane 1 alone and makes pane 2 active); existing tests retargeted to pane state.
egui 0.36 note: there is no `SidePanel`/`TopBottomPanel` — one `egui::Panel` with `left/right/top/bottom`
ctors, `exact_size`/`default_size`/`resizable`, and `show_inside` is deprecated in favour of `show`.
Still not there: volume/mute UI, lazy folder tree.

**Slice 8 (verified):** file operations through `gallerybrowser-fileops`, plus the layout work dual pane
needed on a real half-screen tile. The app owns a `FileOpsWorker`; Ctrl+C/Ctrl+X stage the active pane's
selection, Ctrl+V copies or moves it into that pane's folder with `ConflictPolicy::KeepBoth` (a numbered
copy, never an overwrite), Delete sends it to the OS trash, F2 renames in place, Ctrl+Z / Ctrl+Y (or
Shift+Ctrl+Z) undo and redo. Ops run on the engine's thread; finished ops rescan both panes, stop playback if
the played file moved away, and report errors in the status bar, which also shows the clipboard and what
`undo:` would give back. `--folder-secondary DIR` starts the second pane at its own folder (Qt's
`last_folder_secondary`).

Layout, because a tiled Hyprland window here is ~930 logical px wide on a 2x display: a nested
`CentralPanel` inside a docked panel claims the whole parent rect, which starved pane 1 to **55 px** (probe
readout `P1 w=55 h=960 e=14` vs `P2 w=525`) while screenshots at wide sizes looked fine. Pane columns are now
allocated (`allocate_ui_with_layout`) rather than nested panels, the outer splitter keeps a draggable
`Panel::right("pane2")` with its default capped so pane 1 keeps ~300 px, and each pane is an id scope with
salted scroll areas (egui's `First/Second use of widget ID` warnings are gone). Long grid names elide to the
cell width.

Evidence, one short window at a time on this machine: rename via F2 + typed name + Enter produced exactly
`renamed.png`, Ctrl+Z restored `up.png`; Delete moved the file to trash and Ctrl+Z brought it back; both
panes render folders and engine thumbnails in a 929 px tiled window with no duplicate-id warnings
(`validation_outputs/m1-slice8-narrow-fixed.png`, `m1-slice8-ids-fixed2.png`); stderr empty throughout.
Tooling notes: `wtype -k Delete` delivers nothing for that key here — `wtype -P Delete -p Delete` does;
modifier combos arrive fine (`-M ctrl -k z -m ctrl`). Tests added after verification drive the real worker:
`copy_paste_and_undo_run_through_the_worker` (paste makes `up (1).png`, undo removes it) and
`trash_undo_and_rename_are_reversible`.
Still not there: multi-select (ops act on one selected entry), OS drag/drop, context menus / toolbar buttons
for these commands, volume/mute UI, lazy folder tree.

**M1 slice 9 — project catalog (ratings, tags, collections) is wired.** `App` opens the engine catalog in
`AppDirs::library_dir()` (`Library::open`, which imports the Python-era SQLite DB once when the catalog is
new); a catalog that cannot open falls back to memory and says so in the status bar instead of stopping the
browse. Ratings are keys `0`–`5` and the five stars in the preview panel; tags and collections show as chips
there, added by a small text modal and removed by clicking the chip. Every finished file operation feeds the
catalog: `relink(moved)` and `forget(removed)` from its `OpOutcome`, so a rating follows a rename or move and
annotations die with a trashed file. Evidence: opening the app creates `~/.local/share/gallerybrowser/library`
(catalog journal + lock owner on disk), pressing `4` with nothing selected reaches the handler and reports
"select a file to rate" (`validation_outputs/m1-slice9-catalog.png`), stderr empty; test
`annotations_follow_a_rename_through_the_worker` rates and tags a file, renames it through the ops worker and
finds rating and tag at the new path. Gates: 199 workspace tests pass, strict clippy and fmt clean.
Still not there: filtering the browser by tag or saved search (`LibraryQuery`/`filter_entries` are unused),
multi-select (annotations act on the one selected entry), DB health surface.

**Review pass on slices 7–8 (both reviewers said FIX-FIRST; fixes verified):** the logic reviewer caught that
`open()` called `begin_batch()` **per pane**, and `begin_batch` invalidates the previous generation's queued
work *and* drops decoded-but-unpolled results — so the second pane wiped the first pane's thumbnails.
Thumbnails are now requested in one batch for the visible panes (`refresh_thumbs`), which also evicts textures
for files that left the screen (the texture manager never dedupes). `outstanding` now counts only requests
the engine will actually run — it dedupes by path+size within a batch and answers a duplicate with nothing —
and a 3 s silence guard ends the repaint drive instead of spinning forever. Selecting a file that has no
decodable preview (filtered out, decode error) no longer waits forever; a finished op rescans only the
folders it touched, keeps a selection whose file still exists, spends a cut, and reports "nothing to undo".
Turning dual pane off returns the active pane to the visible one. The layout reviewer found the same starvation
class from the other side: the docked pane had no ceiling and the in-pane columns had floors bigger than the
space, so `pane2` gets a `max_size` and the column floors go to zero; grid cells are clamped to their column;
shortcuts are gated on real keyboard focus (and the rename box closes when it loses focus); the window minimum
dropped to 640×480 logical, which at 2x is a sane physical floor a tiling compositor can still ignore.
Evidence after the fixes: same 929 px tiled window renders both panes fully with elided names and no
duplicate-id warnings (`validation_outputs/m1-slice8-reviewfix.png`), **idle CPU after thumbnails settle is 0%
of one core** (was a per-frame repaint), stderr empty, 8 tests + strict clippy + fmt green.

## Non-goals

No FFI/C ABI shim (full port makes it dead weight). No Qt UI changes beyond keeping it building. No
new decoders written in this repo — decoder work is upstream ArtCraft's.
