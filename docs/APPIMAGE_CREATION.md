# AppImage Creation Guide for KAssetManager

This guide documents the complete process for creating a working AppImage of KAssetManager on Fedora 43 (or similar modern Linux distributions).

## Overview

AppImage packaging bundles the application with its dependencies into a single executable file that runs on most Linux distributions. The current Fedora 43 KDE Wayland baseline has working video playback, timeline scrubbing, hover scrubbing, thumbnails, and live Wayland/tlRender raster video color matching VLC/ffmpeg thumbnails after corrected YUV chroma scaling. AppImage video thumbnails use external `/usr/bin/ffmpeg` extraction for robust process isolation. The process involves:

1. Building the application with proper CMake configuration
2. Installing to a staging directory (AppDir)
3. Using linuxdeploy or AppImage tools to bundle dependencies
4. Creating the final AppImage

## Prerequisites

### Required Tools

- **CMake 3.20+**
- **Ninja build system**
- **Qt 6 development packages** (Qt 6.5+)
- **GCC or Clang compiler** with C++20 support
- **appimagetool** (for creating the final AppImage)

### Optional Tools

- **linuxdeploy** (for automatic dependency bundling)
- **linuxdeploy-plugin-qt** (for Qt-specific bundling)

You can find these tools in your package manager or download official AppImage versions:

```bash
# Download official AppImage tools to project-local directory
mkdir -p tools/appimage
cd tools/appimage
curl -L -o appimagetool-x86_64.AppImage https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
curl -L -o linuxdeploy-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -L -o linuxdeploy-plugin-qt-x86_64.AppImage https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x *.AppImage
cd ../..
```

## Step-by-Step Build Process

### Step 1: Clean Build Environment

**Critical**: Always start with a clean build directory. Stale CMake caches can cause issues with incorrect paths (e.g., FFmpeg pointing to non-existent `.worktrees/` directories).

```bash
# Remove old build artifacts
rm -rf /home/npittas/KAssetManager/build-linux-appimage
mkdir -p /home/npittas/KAssetManager/build-linux-appimage
```

### Step 2: Configure with CMake

**Important Flags**:
- `-DCMAKE_INSTALL_LIBDIR=lib`: Fedora defaults to `lib64`, but tlRender installs libraries to `lib/`. This mismatch causes segfaults because the binary's RUNPATH (`$ORIGIN/../lib64`) points to a non-existent directory.
- `-DCMAKE_PREFIX_PATH`: Point to the tlRender installation directory
- `-DCMAKE_BUILD_TYPE=Release`: Create optimized release build

```bash
cd /home/npittas/KAssetManager

cmake -S native/qt6 -B build-linux-appimage -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_APP=ON \
  -DBUILD_TESTS=OFF \
  -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="/home/npittas/KAssetManager/third_party/tlRender-install-Release"
```

### Step 3: Build the Application

```bash
ninja -j$(nproc) -C /home/npittas/KAssetManager/build-linux-appimage
```

This creates the executable at `build-linux-appimage/kassetmanagerqt`

### Step 4: Install to AppDir

The build helper installs directly into the AppDir staging tree. The installed executable receives the package-layout `qt.conf` automatically; do not place that file beside the build-tree executable used for local development runs.

```bash
cmake --install /home/npittas/KAssetManager/build-linux-appimage \
  --prefix /home/npittas/KAssetManager/build-linux-appimage/AppDir/usr
```

This creates the AppDir structure:
- `AppDir/usr/bin/kassetmanagerqt` (main executable)
- `AppDir/usr/lib/` (libraries)
- `AppDir/usr/plugins/` (Qt plugins)
- `AppDir/usr/qml/` (Qt QML files, if needed)

### Step 5: Create the AppImage

There are two approaches:

#### Option A: Using linuxdeploy (Recommended, if it works)

```bash
cd /home/npittas/KAssetManager

export APPIMAGETOOL="$PWD/tools/appimage/appimagetool-x86_64.AppImage"
export LINUXDEPLOY="$PWD/tools/appimage/linuxdeploy-x86_64.AppImage"
export LINUXDEPLOY_PLUGIN_QT="$PWD/tools/appimage/linuxdeploy-plugin-qt-x86_64.AppImage"
export NO_STRIP=1  # Fedora 43 uses newer ELF format

bash scripts/package-appimage.sh
```

**Known Issues with linuxdeploy**:
- May fail with "Failed to run plugin: qt (exit code: 1)"
- Sometimes has issues with `qmake -query`

#### Option B: Using qtpaths fallback (More reliable)

If linuxdeploy fails, use the fallback method that copies Qt plugins using `qtpaths`:

```bash
cd /home/npittas/KAssetManager

export APPIMAGETOOL="$PWD/tools/appimage/appimagetool-x86_64.AppImage"
export LINUXDEPLOY=""  # Disable linuxdeploy
export LINUXDEPLOY_PLUGIN_QT=""  # Disable plugin
export NO_STRIP=1  # Avoid ELF format issues

bash scripts/package-appimage.sh
```

### Step 6: Verify the AppImage

```bash
# Extract and test (doesn't require display)
cd /tmp
rm -rf appimage-test &>/dev/null
/home/npittas/KAssetManager/KAssetManager-2.0-x86_64.AppImage --appimage-extract

# Test with offscreen platform (no display needed)
cd squashfs-root/usr/bin
QT_QPA_PLATFORM=offscreen timeout 5 ./kassetmanagerqt 2>&1

# Should print Qt messages and exit with code 124 (timeout), NOT 139 (segfault)
```

## Troubleshooting

### Common Failure Modes

| Symptom | Cause | Fix |
|--------|-------|-----|
| **Segfault (Exit 139)** | RUNPATH points to `lib64` but libraries in `lib/` | Add `-DCMAKE_INSTALL_LIBDIR=lib` to cmake |
| **"file INSTALL cannot find qt.conf"** | Outdated build files from older CMake packaging logic | Reconfigure with the current branch and rerun `scripts/build-linux-appimage.sh` |
| **linuxdeploy-plugin-qt: "Failed to run plugin"** | linuxdeploy can't query qmake | Set `LINUXDEPLOY=""` to use fallback |
| **Incompatible library versions bundled** | Multiple FFmpeg versions (.61, .62, .59) | Remove duplicates before packaging |
| **Stales CMake cache** | FFmpeg_CFLAGS points to `.worktrees/` | Wipe build directory and reconfigure |

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

Fedora 43 uses a newer ELF format with `.relr.dyn` sections that older `strip` tools don't recognize. This causes linuxdeploy to fail when trying to strip libraries. Setting `NO_STRIP=1` skips the stripping step.

## Scripts Reference

### Build Script (`scripts/build-linux-appimage.sh`)

Handles CMake configuration and build. Key features:
- Uses Ninja generator
- Sets proper install prefix
- Configures for AppImage packaging

### Package Script (`scripts/package-appimage.sh`)

Handles AppImage creation with:
- linuxdeploy support (if available)
- qtpaths fallback (if linuxdeploy fails)
- Proper environment setup
- AppImage generation

## Final AppImage Location

After successful build:
- **Main AppImage**: `/home/npittas/KAssetManager/KAssetManager-2.0-x86_64.AppImage`

This file is ready to distribute and run on most Linux distributions.

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
