# AppImage Creation Guide — KAssetManager

How to build a self-contained KAssetManager AppImage on Linux.

## Overview

The AppImage bundles the application **plus its full Qt 6 runtime** (~24 Qt
libraries), tlRender's FFmpeg 8 libraries, and the Qt Multimedia FFmpeg 9
backend. It does **not** depend on the host's Qt — this is a hard requirement:
an earlier image that relied on host Qt died at startup on Arch/Omarchy
(glibc ≥ 2.41 treats a copy relocation against Qt's protected
`_ZTI13QGraphicsItem` symbol as a fatal loader error when the binary was linked
against a different distro's Qt build).

Two scripts do all the work:

1. `scripts/build-linux-appimage.sh` — CMake configure + build + install into
   the AppDir staging tree
2. `scripts/package-appimage.sh` — deploys Qt and creates the final AppImage
   with linuxdeploy + linuxdeploy-plugin-qt

## Prerequisites

### Build host

- **Arch Linux / Omarchy** is the validated baseline (Qt 6.11, CMake 4.4,
  glibc 2.44). Any current rolling distro with a similar stack should work.
  **Important:** the binary must be linked against the same Qt that gets
  bundled, so build and package on the same machine — do not mix a Fedora-built
  binary with an Arch-bundled Qt (see Overview).
- glibc of the build host becomes the minimum for the AppImage (currently
  2.44). Older LTS distros will not run it.
- `fuse2` — required to *run* AppImage tooling and the final AppImage itself

### Packages (Arch)

```bash
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-multimedia \
    qt6-svg qt6-declarative ffmpeg fuse2
```

### One-time tool setup

linuxdeploy, linuxdeploy-plugin-qt and appimagetool are downloaded into the
project-local `tools/appimage/` directory. The packaging script expects them
on `PATH` under their plain names, so create symlinks once:

```bash
mkdir -p tools/appimage && cd tools/appimage
curl -L -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
curl -L -O https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x *.AppImage
ln -sf linuxdeploy-x86_64.AppImage linuxdeploy
ln -sf linuxdeploy-plugin-qt-x86_64.AppImage linuxdeploy-plugin-qt
ln -sf appimagetool-x86_64.AppImage appimagetool
```

(`tools/appimage/` is untracked; every contributor does this once.)

## Build and Package

```bash
cd /home/npittas/KAssetManager

# 1. Build + install into AppDir staging (clean dir on first build)
scripts/build-linux-appimage.sh

# 2. Deploy Qt + create the AppImage
PATH="$PWD/tools/appimage:$PATH" scripts/package-appimage.sh

# Result: KAssetManager-<version>-x86_64.AppImage in the repo root
```

Useful environment variables: `BUILD_DIR`, `CMAKE_BUILD_PARALLEL_LEVEL`,
`APPIMAGE_NAME` (output filename), `LINUXDEPLOY`, `LINUXDEPLOY_PLUGIN_QT`,
`APPIMAGETOOL` (explicit tool paths, overriding `PATH` lookup).

## Verify

```bash
./KAssetManager-2.0-x86_64.AppImage
```

- A window opens; the app log appears at
  `~/.local/share/KAsset/KAsset Manager Qt/app.log`
- Qt must load from the AppImage mount, not the host. While it runs:

```bash
pid=$(pgrep -x kassetmanagerqt | head -1)
grep -m1 libQt6Core.so.6 /proc/$pid/maps   # expect /tmp/.mount_*/usr/lib/...
```

- Headless smoke test (no window):

```bash
QT_QPA_PLATFORM=offscreen timeout 5 ./build-linux-appimage/AppDir/usr/bin/kassetmanagerqt
# exit code 124 = survived (good); 139/134 = crash
```

## What the packaging script does (and why)

`scripts/package-appimage.sh` runs `linuxdeploy --plugin qt`, which bundles the
Qt libraries and plugins the app needs. linuxdeploy-plugin-qt resolves plugin
deployment **inside the AppDir**, so the script pre-stages a curated plugin set
from the host Qt into `AppDir/usr/lib/qt6/plugins/` first:

- `platforms/`: `libqwayland.so`, `libqxcb.so` (Qt 6 has a single unified
  Wayland plugin — the Qt 5-era `libqwayland-egl`/`libqwayland-generic` names
  no longer exist)
- `imageformats/`: gif, ico, jpeg, svg
- `iconengines/`: svg icon engine
- `sqldrivers/`: sqlite only (ibase/odbc/mysql/psql drivers would drag in
  optional system libraries like Firebird)
- `multimedia/`: the FFmpeg media backend (built against host FFmpeg 9,
  `.so.63` — coexists with tlRender's bundled FFmpeg 8 `.so.62`)
- `wayland-decoration-client/`, `wayland-graphics-integration-client/`,
  `wayland-shell-integration/`

It also creates an empty `AppDir/usr/lib/qt6/qml/` (the app links QtQuick but
ships no QML sources) and exposes the host's `qmlimportscanner` via a `PATH`
shim (`tools/appimage/qt-host-bin/`) because Arch installs it outside `PATH`.

### Fallback: qtpaths path (no linuxdeploy)

If linuxdeploy / linuxdeploy-plugin-qt are not on `PATH`, the script falls
back to a controlled copy via `qtpaths`: it copies the same Qt plugin
categories from the host Qt install and copies **all** `libQt6*.so.6`
runtime libraries into `AppDir/usr/lib/` (`copy_qt_runtime_libraries`), so the
image does not depend on the host's Qt either. Caveat: unlike the linuxdeploy
path, plugin dependency closures are not resolved — non-Qt system libraries
the plugins need (xkbcommon, xcb, wayland client libs, …) are *not* bundled,
so this path is best when building and running on similar distros.

## Build-system notes

- **CMake 4 + vendored deps:** `third_party/tlRender-install-Release` installs
  several dependencies (Imath, minizip-ng, OpenEXR, OpenImageIO, SDL2, …) under
  `lib64/cmake/`. CMake 4 does not search `<prefix>/lib64/cmake/` for
  `CMAKE_PREFIX_PATH` entries, so `native/qt6/CMakeLists.txt` registers each
  vendored config dir explicitly as `<pkg>_DIR` before `find_package(tlRender)`.
- **`-DCMAKE_INSTALL_LIBDIR=lib`** (set by the build script): keeps the install
  layout matching tlRender's `lib/` so the binary's RUNPATH resolves.
- The **binary must be built on the same distro/Qt it ships with**. A binary
  linked against Fedora's Qt aborts at load time when combined with Arch's Qt
  even inside the AppImage, because of the protected-symbol copy-relocation
  check described in the Overview.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Exit 127 before main, `_ZTI13QGraphicsItem` / `GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS` error | AppImage depends on host Qt, binary linked against another distro's Qt | Rebuild **and** repackage on the same machine; ensure linuxdeploy branch ran (not the qtpaths fallback) |
| `Cannot deploy non-existing library file: .../AppDir/usr/lib/qt6/plugins/...` | Plugin not pre-staged | Add it to the staging list in `scripts/package-appimage.sh` |
| `Could not find dependency: libfbclient.so.2` (or libodbc/libpq) | Non-curated sqldrivers staged | Keep only `libqsqlite.so` in the staging list |
| `qmlimportscanner not found` | Arch installs it outside `PATH` | Ensure the `qt-host-bin` shim ran; check `tools/appimage/qt-host-bin/qmlimportscanner` exists |
| CMake: `Could not find package configuration file provided by "minizip-ng"` | Stale `-NOTFOUND` cache entries | Delete the build dir and reconfigure |
| Segfault (exit 139) from stale RUNPATH | Old build cache | `rm -rf build-linux-appimage` and rebuild |

## AppImage runtime behavior

- Per-user data lives under `~/.local/share/KAsset/KAsset Manager Qt/`
  (AppImage runs never use the portable in-tree data root).
- Video thumbnails use the external `/usr/bin/ffmpeg` for robust process
  isolation; tlRender handles in-app playback.
- `mpv` is not an active Linux playback backend.

## See Also

- `docs/INSTALL.md` — general installation instructions
- `docs/linux-wayland-validation.md` — Linux/Wayland validation notes
- `docs/DEVELOPER_GUIDE.md` — developer setup and contribution guide
