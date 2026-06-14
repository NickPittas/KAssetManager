# KAsset Manager

Native Qt 6 asset manager for browsing, previewing, tagging, rating, and organizing production media on Windows and Linux. The current validated Linux baseline is Fedora KDE/Wayland with AppImage packaging.

## Current status

- Linux AppImage artifact: `KAssetManager-2.0-x86_64.AppImage`
- Linux validation baseline: Fedora KDE/Wayland
- Runtime stack: Qt 6, SQLite, tlRender/OpenImageIO/FFmpeg-backed preview paths
- AppImage packaging uses a controlled AppDir flow that bundles Qt runtime libraries/plugins and app media libraries while leaving low-level system-coupled libraries such as `libudev`/`libsystemd` to the host.

## Key features

### File Manager

- Dual-pane explorer layout with persistent secondary-pane path, view mode, preview size, and splitter sizes.
- Folder tree, favorites, path bar, and folder double-click navigation target whichever explorer pane is active.
- Grid and list views with folders-first sorting.
- Built-in folder/drive/file fallback icons so packaged AppImages do not depend on host icon themes for the tree/list folder icons.
- Ctrl-hover scrubbing over grid cards for videos and image sequences, including secondary panes.

### Asset Library

- Virtual folders for organizing assets without moving files on disk.
- Drag-and-drop import plus File Manager “Add to Library”. Folder imports preserve hierarchy.
- Multi-tag assignment, tag management, 5-star ratings, and combined filtering.
- Search and sortable grid/list views.

### Preview and playback

- Image preview with zoom/pan.
- Video and image-sequence playback with timeline, volume, frame stepping, and scrubbing.
- Live preview streaming through the Linux/tlRender/FFmpeg path with OpenImageIO for still images.
- Annotation tools for frame-accurate review workflows.

## Build and package on Linux

### Build AppDir

```bash
scripts/build-linux-appimage.sh
```

This configures `native/qt6`, builds the release target, and installs into:

```text
build-linux-appimage/AppDir/usr
```

### Create AppImage

`appimagetool` is required. Keep downloaded packaging tools project-local under `tools/appimage/` if you use AppImage downloads.

```bash
APPIMAGETOOL=tools/appimage/appimagetool-x86_64.AppImage \
  scripts/package-appimage.sh
```

The package script:

- creates `AppRun`
- copies desktop/icon metadata
- copies Qt runtime libraries and plugins through `qtpaths` when linuxdeploy is not explicitly configured
- supports linuxdeploy + Qt plugin, but the controlled `qtpaths` path is preferred on Fedora because broad linuxdeploy dependency sweeps can bundle incompatible system-coupled libraries
- writes `KAssetManager-2.0-x86_64.AppImage` in the repository root

### Smoke test the AppImage

A successful GUI launch should stay alive until killed by `timeout`; an immediate `139` exit means a launch crash.

```bash
timeout 8 ./KAssetManager-2.0-x86_64.AppImage
```

Check unresolved dependencies from the staged binary:

```bash
LD_LIBRARY_PATH=build-linux-appimage/AppDir/usr/lib \
  ldd build-linux-appimage/AppDir/usr/bin/kassetmanagerqt
```

## Windows build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
.\dist\portable\bin\kassetmanagerqt.exe
```

## Project structure

```text
native/qt6/                  Qt 6 C++ application
native/qt6/src/              Main app, file manager, preview, database, import, models
native/qt6/player_lab/       GPU/FFmpeg player experiments and validation harnesses
scripts/                     Linux AppImage and platform build helpers
docs/                        User, install, architecture, dependency, and packaging docs
third_party/                 Checked-in third-party runtime/development payloads used by builds
build-linux-appimage/        Generated AppDir/build staging; ignored and safe to delete
native/qt6/build*/           Generated local CMake build trees; ignored and safe to delete
tools/appimage/              Downloaded local packaging tools; ignored and reproducible
```

## Documentation

- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) — user workflows and UI behavior
- [docs/INSTALL.md](docs/INSTALL.md) — setup and installation notes
- [docs/APPIMAGE_CREATION.md](docs/APPIMAGE_CREATION.md) — Linux AppImage build/package/verification workflow
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — application architecture
- [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) — dependency and security notes
- [codemap.md](codemap.md) — generated repository navigation snapshot; use it for orientation only, then inspect live files before editing

## Cleanup policy

Generated build outputs, AppImages, local packaging tools, scratch plans, graph exports, recorder sessions, screenshots, and validation captures are not source-of-truth. They should remain ignored/untracked and can be regenerated from scripts and docs.

Tracked source, documentation, build scripts, and relocatable third-party package metadata are source-of-truth and should be committed when changed intentionally.

## System requirements

- Windows 10/11 64-bit or modern 64-bit Linux
- 4 GB RAM minimum, 8 GB+ recommended
- 500 MB+ application/runtime disk footprint plus media/cache space
- GPU/OpenGL stack suitable for Qt and video preview

## License

Proprietary - All rights reserved.
