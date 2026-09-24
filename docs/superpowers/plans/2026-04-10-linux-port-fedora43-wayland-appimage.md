# Fedora 43 Wayland AppImage Linux Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port KAsset Manager to Fedora Linux 43 KDE Wayland and ship it as an AppImage while preserving all core workflows and differentiating features except Everything Search.

**Architecture:** Keep the existing Qt Widgets + tlRender architecture and port the platform-specific integrations to Linux/Wayland instead of replacing core subsystems. Execute the work gate-first: prove native Wayland media/runtime behavior on Fedora 43, then port Linux file-manager integration and AppImage packaging, and finally verify all required workflows against the real test corpus in `/mnt/ssd2/Tests/`.

**Tech Stack:** Qt 6 Widgets, CMake, QtTest, tlRender, OpenImageIO, SQLite, FFmpeg, ImageMagick, D-Bus freedesktop integration, AppImage/linuxdeploy, Fedora 43 KDE Wayland

---

## Scope And Acceptance Contract

The port is only accepted when all of these work on Fedora 43 KDE Wayland from both a local build and the final AppImage:

- Native Wayland app launch
- Video playback
- Image-sequence playback
- Timeline scrubbing for video and sequences
- Annotation on video and sequences
- Video and sequence thumbnail generation
- Project Manager watch/import/version workflows
- Batch rename
- Standard Linux file operations: copy, move, paste, trash, permanent delete, rename, new folder
- Reveal/select in the Linux file manager
- Bundled `ffmpeg` and `magick` conversions
- AppImage runtime reproduces the same behaviors

Everything Search may be omitted if no Linux-equivalent implementation reaches acceptable quality.

## Hard Gates

- `tlRender` + Qt 6 + Wayland runtime
- Video playback on Wayland
- Sequence playback on Wayland
- Annotation overlay correctness above the GL/video surface
- Video thumbnail extraction via `TLRenderPlayer`
- AppImage bundling of Qt Wayland plugins, tlRender, and media dependencies

## Non-Gates

- Project Manager DB/model/watcher logic
- Batch rename
- Sequence grouping/version grouping logic
- Sequence thumbnail generation from image frames
- Standard file CRUD behavior

## Validation Assets

Use these exact files during manual validation:

- Documents:
  - `/mnt/ssd2/Tests/Documents/4-ypeythini-dilosi-pragmatikwn-dikaiouxwn-1.docx`
  - `/mnt/ssd2/Tests/Documents/7Shots_Combined_AfterLife_BID_v14JN_UPDATED_22-10-24- Juice Scope.xlsx`
  - `/mnt/ssd2/Tests/Documents/6k_2023_deyterobaumias_ekpaideyshs_elenh.eleyuerakh1.pdf`
- Images:
  - `/mnt/ssd2/Tests/Images/WARM_0020a_comp_10K_LL180_acescg_lin_v0047_1001.exr`
  - `/mnt/ssd2/Tests/Images/Big_Hole_01.jpg`
  - `/mnt/ssd2/Tests/Images/Burn_1_Front_Dry.png`
  - `/mnt/ssd2/Tests/Images/Shotgun Drywall 1.png`
- Sequence:
  - `/mnt/ssd2/Tests/Images/HD/Sunshine_10_sec_RoopHD_0303.png` through `/mnt/ssd2/Tests/Images/HD/Sunshine_10_sec_RoopHD_0360.png`
- Videos:
  - `/mnt/ssd2/Tests/Videos/MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
  - `/mnt/ssd2/Tests/Videos/Blood_Hit_03.mov`
  - `/mnt/ssd2/Tests/Videos/Sunshine 10sec Full Comp v3.mov`
  - `/mnt/ssd2/Tests/Videos/Atmosphere-019.mov`
- Projects:
  - `/mnt/ssd2/Tests/Projects/WARM_0010_fade in_test.v001.nk`

## Files To Modify

### Build and packaging
- Modify: `native/qt6/CMakeLists.txt`
- Modify: `native/qt6/cmake/DeployQt.cmake`
- Create: `scripts/build-linux-appimage.sh`
- Create: `scripts/package-appimage.sh`
- Create: `native/qt6/packaging/linux/kassetmanager.desktop`
- Create: `native/qt6/packaging/linux/AppRun.env`
- Modify/create: `.github/workflows/linux-appimage.yml`

### Wayland/media/runtime
- Modify: `native/qt6/src/main.cpp`
- Modify: `native/qt6/src/preview_overlay.cpp`
- Modify: `native/qt6/src/live_preview_manager.cpp`
- Modify: `native/qt6/src/media/tlrender_player.cpp`
- Modify: `native/qt6/src/media/tlrender_widget.cpp`
- Modify: `native/qt6/src/media/tlrender_viewport.cpp`
- Modify: `native/qt6/src/thumbnail_generator_worker.cpp`

### Linux shell/file integration
- Modify: `native/qt6/src/file_ops.h`
- Modify: `native/qt6/src/file_ops.cpp`
- Modify: `native/qt6/src/drag_utils.cpp`
- Modify: `native/qt6/src/mainwindow.cpp`

### Preview/docs/project workflows
- Modify: `native/qt6/src/office_preview.cpp`
- Modify: `native/qt6/src/media_convert_dialog.cpp`
- Modify: `native/qt6/src/media_converter_worker.cpp`
- Modify only if needed: `native/qt6/src/project_manager_watcher.cpp`
- Modify only if needed: `native/qt6/src/project_db.cpp`
- Modify only if needed: `native/qt6/src/project_assets_model.cpp`
- Modify only if needed: `native/qt6/src/project_sequence_grouping_proxy_model.cpp`
- Modify only if needed: `native/qt6/src/bulk_rename_dialog.cpp`

### Tests and docs
- Modify/create: `native/qt6/tests/test_live_preview_manager.cpp`
- Modify/create: `native/qt6/tests/test_models.cpp`
- Modify/create: `native/qt6/tests/test_project_manager.cpp`
- Create: `docs/linux-wayland-validation.md`
- Modify: `README.md`
- Modify: `docs/INSTALL.md`
- Modify: `docs/DEVELOPER_GUIDE.md`
- Modify: `AGENTS.md`

### Task 1: Establish Fedora 43 Wayland App Build Baseline

**Files:**
- Modify: `native/qt6/CMakeLists.txt`
- Modify: `native/qt6/cmake/DeployQt.cmake`
- Modify: `native/qt6/src/main.cpp`

- [ ] **Step 1: Write the failing baseline verification**

Run:
```bash
cmake -S native/qt6 -B build-linux -G Ninja -DBUILD_APP=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
```

Expected: configuration fails on current Windows-only assumptions such as tlRender layout, `zlib.lib`, or Windows deployment logic.

- [ ] **Step 2: Implement minimal Linux-safe CMake configuration**

Requirements:
- remove shared references to Windows-only import libs such as `zlib.lib`
- guard Windows deployment logic under `WIN32`
- support Linux tlRender via `TLRENDER_ROOT` or `CMAKE_PREFIX_PATH`
- keep existing Windows behavior intact

- [ ] **Step 3: Re-run configuration to verify it passes**

Run:
```bash
cmake -S native/qt6 -B build-linux -G Ninja -DBUILD_APP=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
```

Expected: configuration succeeds on Fedora 43.

- [ ] **Step 4: Build the app and tests**

Run:
```bash
cmake --build build-linux -j 2
```

Expected: app and test targets compile successfully.

- [ ] **Step 5: Verify native Wayland startup**

Run:
```bash
./build-linux/kassetmanagerqt
```

Expected:
- app launches in the current Wayland session
- no immediate crash
- no missing Qt Wayland plugin errors
- OpenGL initialization succeeds

- [ ] **Step 6: Commit**

```bash
git add native/qt6/CMakeLists.txt native/qt6/cmake/DeployQt.cmake native/qt6/src/main.cpp
git commit -m "build: enable Fedora Wayland app configure and startup"
```

### Task 2: Prove tlRender Video And Sequence Playback On Wayland

**Files:**
- Modify: `native/qt6/src/preview_overlay.cpp`
- Modify: `native/qt6/src/live_preview_manager.cpp`
- Modify: `native/qt6/src/media/tlrender_player.cpp`
- Modify: `native/qt6/src/media/tlrender_widget.cpp`
- Modify: `native/qt6/src/media/tlrender_viewport.cpp`
- Test: `/mnt/ssd2/Tests/Videos/*` and `/mnt/ssd2/Tests/Images/HD/*`

- [ ] **Step 1: Run failing manual playback validation**

Validate these inputs:
- `/mnt/ssd2/Tests/Videos/MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
- `/mnt/ssd2/Tests/Videos/Sunshine 10sec Full Comp v3.mov`
- `/mnt/ssd2/Tests/Images/HD/Sunshine_10_sec_RoopHD_0303.png ... 0360.png`

Record any failures in:
- viewport creation
- frame presentation
- play/pause
- seek
- frame stepping
- sequence grouping/playback

- [ ] **Step 2: Implement the minimal playback fixes**

Fix only what is required for stable Wayland playback:
- tlRender initialization order
- OpenGL context/surface handling
- viewport/player hookup
- seek/frame-step correctness

- [ ] **Step 3: Re-run manual validation**

Expected:
- both video and image sequence playback work correctly on Fedora 43 Wayland

- [ ] **Step 4: Run focused tests**

Run:
```bash
cmake --install build-linux --prefix build-linux/install_run
ctest --test-dir build-linux --output-on-failure -R test_live_preview
```

Expected: live preview related tests pass after install.

- [ ] **Step 5: Commit**

```bash
git add native/qt6/src/preview_overlay.cpp native/qt6/src/live_preview_manager.cpp native/qt6/src/media/tlrender_player.cpp native/qt6/src/media/tlrender_widget.cpp native/qt6/src/media/tlrender_viewport.cpp native/qt6/tests
git commit -m "fix: restore tlRender playback on Fedora Wayland"
```

### Task 3: Preserve Annotation On Video And Sequences

**Files:**
- Modify: `native/qt6/src/preview_overlay.cpp`
- Modify: `native/qt6/src/annotation_layer.cpp`
- Modify: `native/qt6/src/annotation_items.cpp`
- Test: sequence/video annotation using `/mnt/ssd2/Tests/Images/HD/*` and `/mnt/ssd2/Tests/Videos/*`

- [ ] **Step 1: Write failing manual validation notes for sequence annotation**

Verify on `/mnt/ssd2/Tests/Images/HD/` sequence:
- toggle annotation mode
- draw on multiple frames
- undo/redo
- frame markers
- save current
- save all

Expected: if anything fails, capture whether it is overlay visibility, event routing, frame drift, or export mismatch.

- [ ] **Step 2: Write failing manual validation notes for video annotation**

Verify on:
- `/mnt/ssd2/Tests/Videos/Blood_Hit_03.mov`
- `/mnt/ssd2/Tests/Videos/Sunshine 10sec Full Comp v3.mov`

- [ ] **Step 3: Implement minimal overlay and frame-sync fixes**

Preserve:
- transparent overlay above the GL surface
- per-frame annotation persistence
- export correctness
- keyboard shortcuts and frame markers

- [ ] **Step 4: Re-run annotation validation**

Expected: annotation behaves correctly for both sequence and video under Wayland.

- [ ] **Step 4a: Restore application and annotation icons on Linux**

Verify and fix:
- file manager toolbar icons
- media transport icons in preview surfaces
- annotation toggle and annotation toolbar icons

Expected: core app navigation and annotation UI are usable on Fedora 43 Wayland local builds and packaged layouts.

- [ ] **Step 5: Commit**

```bash
git add native/qt6/src/preview_overlay.cpp native/qt6/src/annotation_layer.cpp native/qt6/src/annotation_items.cpp
git commit -m "fix: preserve annotation workflows on Wayland"
```

### Task 4: Preserve Thumbnail Generation And Scrubbing

**Files:**
- Modify: `native/qt6/src/thumbnail_generator_worker.cpp`
- Modify: `native/qt6/src/live_preview_manager.cpp`
- Modify: `native/qt6/src/grid_scrub.cpp`
- Modify: `native/qt6/src/asset_item_delegate.cpp`
- Modify: `native/qt6/src/fm_item_delegate.cpp`

- [ ] **Step 1: Write failing validation for still, sequence, and video thumbnails**

Use:
- `/mnt/ssd2/Tests/Images/WARM_0020a_comp_10K_LL180_acescg_lin_v0047_1001.exr`
- `/mnt/ssd2/Tests/Images/Burn_1_Front_Dry.png`
- `/mnt/ssd2/Tests/Images/HD/*`
- `/mnt/ssd2/Tests/Videos/Atmosphere-019.mov`

Verify:
- Generate Thumbnail action
- persistent cache creation
- hover/grid scrubbing
- timeline scrubbing

- [ ] **Step 2: Implement minimal fixes for tlRender-backed video thumbnails and scrub position mapping**

- [ ] **Step 3: Re-run thumbnail and scrubbing validation**

Expected:
- image, sequence, and video thumbnails generate correctly
- video and sequence scrubbing remain responsive and accurate

- [ ] **Step 4: Commit**

```bash
git add native/qt6/src/thumbnail_generator_worker.cpp native/qt6/src/live_preview_manager.cpp native/qt6/src/grid_scrub.cpp native/qt6/src/asset_item_delegate.cpp native/qt6/src/fm_item_delegate.cpp
git commit -m "fix: restore thumbnails and scrubbing on Linux"
```

### Task 5: Port Linux File Operations And File Manager Reveal

**Files:**
- Modify: `native/qt6/src/file_ops.h`
- Modify: `native/qt6/src/file_ops.cpp`
- Modify: `native/qt6/src/drag_utils.cpp`
- Modify: `native/qt6/src/mainwindow.cpp`

- [ ] **Step 1: Write the failing manual verification checklist**

Validate:
- copy
- cut
- paste
- move
- delete to trash
- permanent delete
- rename
- new folder
- reveal/select in file manager

Use a disposable scratch directory created near `/mnt/ssd2/Tests/` rather than mutating the corpus.

- [ ] **Step 2: Implement Linux-native file-manager semantics**

Requirements:
- use Qt filesystem operations for copy/move/rename/new folder
- use `QFile::supportsMoveToTrash()` and `QFile::moveToTrash()` for trash where supported
- use D-Bus `org.freedesktop.FileManager1.ShowItems` for reveal/select
- preserve queue/progress/error UX in `FileOpsQueue`

- [ ] **Step 3: Re-run manual file operation validation**

Expected: operations behave like a normal Linux file manager while preserving the app’s queue/progress UX.

- [ ] **Step 4: Commit**

```bash
git add native/qt6/src/file_ops.h native/qt6/src/file_ops.cpp native/qt6/src/drag_utils.cpp native/qt6/src/mainwindow.cpp
git commit -m "feat: add Linux-native file operations and file manager reveal"
```

### Task 6: Verify Project Manager And Batch Rename

**Files:**
- Modify only if needed: `native/qt6/src/project_manager_watcher.cpp`
- Modify only if needed: `native/qt6/src/project_db.cpp`
- Modify only if needed: `native/qt6/src/project_assets_model.cpp`
- Modify only if needed: `native/qt6/src/project_sequence_grouping_proxy_model.cpp`
- Modify only if needed: `native/qt6/src/bulk_rename_dialog.cpp`

- [ ] **Step 1: Run Project Manager validation against `/mnt/ssd2/Tests/`**

Verify:
- create/open project rooted at `/mnt/ssd2/Tests/`
- watch path
- detect new files
- browse folders
- version grouping for `/mnt/ssd2/Tests/Projects/WARM_0010_fade in_test.v001.nk`
- preview project assets

- [ ] **Step 2: Run bulk rename validation on a scratch copy of image files**

Verify:
- rename pattern preview
- token handling
- duplicate conflict handling
- filesystem rename
- DB update where relevant

- [ ] **Step 3: Implement only Linux-specific fixes found**

- [ ] **Step 4: Run focused tests**

Run:
```bash
cmake --install build-linux --prefix build-linux/install_run
ctest --test-dir build-linux --output-on-failure -R "test_models|test_db"
```

Expected: model and DB tests pass.

- [ ] **Step 5: Commit**

```bash
git add native/qt6/src/project_manager_watcher.cpp native/qt6/src/project_db.cpp native/qt6/src/project_assets_model.cpp native/qt6/src/project_sequence_grouping_proxy_model.cpp native/qt6/src/bulk_rename_dialog.cpp native/qt6/tests
git commit -m "fix: validate project manager and bulk rename on Linux"
```

### Task 7: Bundle And Verify Converters

**Files:**
- Modify: `native/qt6/src/media_convert_dialog.cpp`
- Modify: `native/qt6/src/media_converter_worker.cpp`
- Create/modify: `scripts/build-linux-appimage.sh`
- Create/modify: `scripts/package-appimage.sh`

- [ ] **Step 1: Write failing validation for conversion behavior**

Use video and image inputs from `/mnt/ssd2/Tests/Videos/` and `/mnt/ssd2/Tests/Images/`.

Verify:
- converter locates binaries
- MP4/MOV outputs work
- PNG/TIF sequence outputs preserve alpha where expected

- [ ] **Step 2: Implement bundled-path-first lookup for `ffmpeg` and `magick`**

Requirement:
- prefer bundled AppImage runtime paths
- fall back to PATH only for developer convenience

- [ ] **Step 3: Re-run conversion validation**

Expected: conversions succeed using Linux-safe runtime lookup.

- [ ] **Step 4: Commit**

```bash
git add native/qt6/src/media_convert_dialog.cpp native/qt6/src/media_converter_worker.cpp scripts
git commit -m "feat: resolve bundled conversion tools on Linux"
```

### Task 8: Package The Fedora 43 Wayland AppImage

**Files:**
- Modify: `native/qt6/CMakeLists.txt`
- Create: `scripts/build-linux-appimage.sh`
- Create: `scripts/package-appimage.sh`
- Create: `native/qt6/packaging/linux/kassetmanager.desktop`
- Create: `native/qt6/packaging/linux/AppRun.env`
- Create/modify: `.github/workflows/linux-appimage.yml`

- [ ] **Step 1: Implement Linux install/AppDir layout**

Bundle:
- executable
- Qt Wayland-capable platform plugins
- imageformats/platforms/styles/plugins needed at runtime
- tlRender runtime libraries
- OpenImageIO runtime dependencies
- bundled `ffmpeg`
- bundled `magick`
- desktop file and icons

- [ ] **Step 2: Install into AppDir**

Run:
```bash
cmake --install build-linux --prefix build-linux/AppDir/usr
```

Expected: AppDir contains a complete Linux runtime layout.

- [ ] **Step 3: Package AppImage**

Run:
```bash
./scripts/package-appimage.sh
```

Expected: a `KAssetManager-*.AppImage` artifact is produced.

- [ ] **Step 4: Smoke-test the AppImage on Fedora 43 Wayland**

Run:
```bash
chmod +x KAssetManager-*.AppImage
./KAssetManager-*.AppImage
```

Expected:
- app launches on Wayland
- no missing platform/plugin/runtime errors

- [ ] **Step 5: Re-run critical feature checks from the AppImage**

Verify again:
- video playback
- sequence playback
- annotation
- thumbnails
- conversion

- [ ] **Step 6: Commit**

```bash
git add native/qt6/CMakeLists.txt scripts native/qt6/packaging/linux .github/workflows
git commit -m "build: package Fedora Wayland AppImage"
```

### Task 9: Document Linux/Fedora 43 Wayland Support

**Files:**
- Create: `docs/linux-wayland-validation.md`
- Modify: `README.md`
- Modify: `docs/INSTALL.md`
- Modify: `docs/DEVELOPER_GUIDE.md`
- Modify: `AGENTS.md`

- [ ] **Step 1: Document Fedora 43 KDE Wayland as the initial validated baseline**
- [ ] **Step 2: Document Linux build prerequisites and tlRender integration**
- [ ] **Step 3: Document AppImage packaging flow**
- [ ] **Step 4: Document the local validation corpus at `/mnt/ssd2/Tests/`**
- [ ] **Step 5: Commit**

```bash
git add docs/linux-wayland-validation.md README.md docs/INSTALL.md docs/DEVELOPER_GUIDE.md AGENTS.md
git commit -m "docs: add Fedora Wayland Linux port guidance"
```

## Final Verification Commands

Run these before declaring the port ready:

```bash
cmake -S native/qt6 -B build-linux -G Ninja -DBUILD_APP=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j 2
cmake --install build-linux --prefix build-linux/install_run
ctest --test-dir build-linux --output-on-failure
./build-linux/kassetmanagerqt
chmod +x KAssetManager-*.AppImage
./KAssetManager-*.AppImage
```

## Manual Validation Checklist

- [ ] Launch app in the current Fedora 43 Wayland session
- [ ] Open `/mnt/ssd2/Tests/Videos/MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
- [ ] Play, pause, seek, and frame-step video playback
- [ ] Open the PNG sequence under `/mnt/ssd2/Tests/Images/HD/`
- [ ] Scrub the sequence timeline and verify frame accuracy
- [ ] Annotate at least 3 different frames in the sequence and export them
- [ ] Annotate at least 3 different frames in a video and export them
- [ ] Generate thumbnails for EXR, PNG sequence, and MOV assets
- [ ] Verify hover scrub/grid scrub works on video and sequence entries
- [ ] Create a project rooted at `/mnt/ssd2/Tests/`
- [ ] Verify the Nuke project appears and version logic remains correct
- [ ] Run bulk rename on a scratch copy of several images
- [ ] Copy, move, trash, permanently delete, and reveal files in the Linux file manager
- [ ] Convert a video and an image sequence using bundled tools
- [ ] Repeat the critical preview/annotation/thumbnail/conversion checks from the AppImage
