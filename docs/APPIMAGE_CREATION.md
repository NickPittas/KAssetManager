# AppImage Creation Guide for KAssetManager

This guide documents the complete process for creating a working AppImage of KAssetManager on Fedora 43 (or similar modern Linux distributions).

## Overview

AppImage packaging stages the application into an AppDir, bundles app/Qt runtime pieces, and emits a single executable. The current Fedora KDE/Wayland baseline has working video playback, timeline scrubbing, hover scrubbing, thumbnails, dual-pane File Manager state, and built-in tree/list folder icons that do not depend on the host icon theme. The process involves:

1. Building the application with the AppImage CMake settings
2. Installing to `build-linux-appimage/AppDir/usr`
3. Copying Qt runtime libraries/plugins through the controlled `qtpaths` path, or using linuxdeploy only when explicitly requested and validated
4. Creating the final AppImage with `appimagetool`

## Prerequisites

### Required Tools

- **CMake 3.20+**
- **Ninja build system**
- **Qt 6 development packages** (Qt 6.5+)
- **GCC or Clang compiler** with C++20 support
- **appimagetool** (for creating the final AppImage)

### Optional Tools

- **linuxdeploy** and **linuxdeploy-plugin-qt** are supported by the script, but the controlled `qtpaths` packaging path is preferred on the current Fedora baseline. Broad linuxdeploy dependency sweeps can bundle low-level system-coupled libraries and produce an AppImage that crashes before `main`.

You can find these tools in your package manager or download official AppImage versions:

```bash
# Download appimagetool to a project-local ignored directory
mkdir -p tools/appimage
cd tools/appimage
curl -L -o appimagetool-x86_64.AppImage https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage
cd ../..
```

## Step-by-Step Build Process

### Step 1: Clean Build Environment

**Critical**: Always start with a clean build directory. Stale CMake caches can cause issues with incorrect paths (e.g., FFmpeg pointing to non-existent `.worktrees/` directories).

```bash
rm -rf build-linux-appimage
```

### Step 2: Build and Install to AppDir

`scripts/build-linux-appimage.sh` configures, builds, and installs the release application into the AppDir staging tree. It sets the important AppImage flags, including `CMAKE_INSTALL_LIBDIR=lib`, so Fedora's `lib64` default does not break the package RUNPATH.

```bash
scripts/build-linux-appimage.sh
```

Expected AppDir structure:
- `AppDir/usr/bin/kassetmanagerqt` (main executable)
- `AppDir/usr/lib/` (app/Qt runtime libraries)
- `AppDir/usr/plugins/` (Qt plugins)
- `AppDir/usr/qml/` (Qt QML files, if copied)

### Step 3: Create the AppImage

Preferred Fedora baseline path:

```bash
APPIMAGETOOL=tools/appimage/appimagetool-x86_64.AppImage \
  scripts/package-appimage.sh
```

This path uses `qtpaths` to copy Qt runtime libraries and plugins into the AppDir, then invokes `appimagetool`.

#### linuxdeploy path

The script still supports linuxdeploy when both variables are set:

```bash
LINUXDEPLOY=tools/appimage/linuxdeploy-x86_64.AppImage \
LINUXDEPLOY_PLUGIN_QT=tools/appimage/linuxdeploy-plugin-qt-x86_64.AppImage \
APPIMAGETOOL=tools/appimage/appimagetool-x86_64.AppImage \
  scripts/package-appimage.sh
```

Validate linuxdeploy-built artifacts carefully. On the current Fedora baseline, linuxdeploy over-bundled low-level system libraries during testing and the resulting AppImage segfaulted before `main`.

### Step 4: Verify the AppImage

```bash
timeout 8 ./KAssetManager-2.0-x86_64.AppImage
```

Expected result: exit code `124` from `timeout`, which means the GUI stayed alive until killed. An immediate `139` exit means a launch crash.

Check staged dependency closure:

```bash
LD_LIBRARY_PATH=build-linux-appimage/AppDir/usr/lib \
  ldd build-linux-appimage/AppDir/usr/bin/kassetmanagerqt
```

Expected result: no `not found` entries.

## Troubleshooting

### Common Failure Modes

| Symptom | Cause | Fix |
|--------|-------|-----|
| **Segfault (Exit 139) immediately on launch** | Incompatible bundled low-level system library or bad RUNPATH | Rebuild from a clean AppDir with the controlled `qtpaths` packaging path and verify no `not found` entries |
| **"file INSTALL cannot find qt.conf"** | Outdated build files from older CMake packaging logic | Reconfigure with the current branch and rerun `scripts/build-linux-appimage.sh` |
| **linuxdeploy-plugin-qt: "Failed to run plugin"** | linuxdeploy cannot query qmake or deploy a plugin | Use the default `qtpaths` path by omitting `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT` |
| **Missing folder/tree/list icons in AppImage** | Runtime depended on host icon theme | Current `icon_utils.cpp` uses generated fallback provider icons; rebuild the app/AppImage |
| **Stale CMake cache** | Build tree points to old absolute worktree paths | Wipe build directory and reconfigure |

### Critical Details

#### Why `CMAKE_INSTALL_LIBDIR=lib` is Required

Fedora defaults `CMAKE_INSTALL_LIBDIR` to `lib64/`, but tlRender installs libraries to `lib/`. When the binary is built with RUNPATH `$ORIGIN/../lib64` but libraries are in `$ORIGIN/../lib`, the dynamic linker fails to find them, causing an immediate segfault.

The AppImage bundling process preserves the binary's RUNPATH, so this mismatch must be fixed at configure time.

#### The `.d` File Problem

Compiler dependency files (`.d`) should **never** be in the source root. They should be in the build tree under `CMakeFiles/`. Old builds from git worktrees may have generated them incorrectly, but modern out-of-source builds with Ninja won't recreate them there.

If you see `a-*.d` files in the root:
- They're from an old build and safe to delete
- They're now in `.gitignore` (`*.d`)
- They won't reappear with current build configuration

#### Using NO_STRIP=1

Fedora uses ELF sections that older strip tools may not recognize. The package script defaults `NO_STRIP=1` for linuxdeploy runs. The controlled `qtpaths` path does not rely on linuxdeploy stripping.

## Scripts Reference

### Build Script (`scripts/build-linux-appimage.sh`)

Handles CMake configuration and build. Key features:
- Uses Ninja generator
- Sets proper install prefix
- Configures for AppImage packaging

### Package Script (`scripts/package-appimage.sh`)

Handles AppImage creation with:
- AppRun, desktop, and icon metadata generation
- Qt runtime library/plugin copying via `qtpaths`
- optional linuxdeploy support when explicitly configured
- AppImage generation through `appimagetool`

## Final AppImage Location

After successful build:
- **Main AppImage**: `KAssetManager-2.0-x86_64.AppImage` in the repository root

The AppImage should be treated as a generated artifact, not committed source.

## Testing on Target Systems

To test the AppImage on different systems:

```bash
# Make executable
chmod +x KAssetManager-2.0-x86_64.AppImage

# Run normally
./KAssetManager-2.0-x86_64.AppImage

# Debug mode
./KAssetManager-2.0-x86_64.AppImage --appimage-extract-and-run
```

## Environment Variables

- `APPIMAGETOOL`: Path to appimagetool binary
- `LINUXDEPLOY`: Path to linuxdeploy binary (set to empty to use fallback)
- `LINUXDEPLOY_PLUGIN_QT`: Path to Qt plugin (set to empty to use fallback)
- `NO_STRIP`: Set to `1` to skip library stripping
- `APPIMAGE_NAME`: Optional output filename override (default derives the project version, e.g. `KAssetManager-2.0-$(uname -m).AppImage`)

## See Also

- `docs/INSTALL.md` - General installation instructions
- `docs/linux-wayland-validation.md` - Linux/Wayland specific validation
- `docs/DEVELOPER_GUIDE.md` - Developer setup and contributions
