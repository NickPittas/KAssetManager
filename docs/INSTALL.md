## Installation and Build Guide

This guide covers building, packaging, and running KAsset Manager on Windows and on the current validated Linux baseline: Fedora 43 KDE Wayland.

### Supported platforms
- Windows 10/11 (64-bit) — full application
- Fedora 43 KDE Wayland — validated Linux runtime baseline and AppImage target
- Linux CI/tests-only configuration also exists for headless verification

### Prerequisites (Windows)
- Visual Studio 2022 or Build Tools (x64)
- Qt 6 (MSVC x64). The build script auto-detects common Qt installs.
- CMake 3.21+ and Ninja (optional; script can build VS or Ninja)
- vcpkg installed at C:\vcpkg (recommended for smoother DLL resolution)
- tlRender build/install (required for playback; default: third_party/tlRender-build/install-Release)
- Optional runtimes (for conversions and advanced formats):
  - FFmpeg (set FFMPEG_ROOT to your build/prefix; used only by the Convert dialog/tools, not for live playback)
  - ImageMagick portable (set IMAGEMAGICK_ROOT; used for single-image conversions)
  - OpenImageIO via vcpkg (optional; enables EXR/PSD/HDR and other advanced formats in LivePreviewManager)
- tlRender runtime DLLs are bundled from the tlRender install and copied automatically into the portable/installer packages; no extra installation is required when using the build script.

### Quick build (Windows, with packaging)

```powershell
# From repository root
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1 -Generator Ninja -Package
```

What it does:
- Configures with CMake (native/qt6)
- Builds Release (Ninja or VS)
- Installs to native/qt6/build/<gen>/install_run
- Verifies the app starts
- Copies required DLLs (Qt, vcpkg, tlRender, optional FFmpeg/ImageMagick)
- Produces a portable folder at dist/portable and a NSIS/ZIP package

Run the application after packaging:

```powershell
.\dist\portable\bin\kassetmanagerqt.exe
```

### Environment variables (optional)
- VCPKG_ROOT: Path to vcpkg (e.g., C:\vcpkg)
- VCPKG_TARGET_TRIPLET: Defaults to x64-windows
- FFMPEG_ROOT: Custom FFmpeg prefix containing include/, lib/, bin/
- IMAGEMAGICK_ROOT (or MAGICK_ROOT): Portable ImageMagick root; script autodetects common folders

### CMake options
- BUILD_APP=ON/OFF — build the GUI app (CI uses OFF)
- BUILD_TESTS=ON/OFF — build unit tests
- ENABLE_ASAN/ENABLE_UBSAN=ON — sanitizers (Clang/GCC)
- ENABLE_CLANG_TIDY=ON — static analysis
- ENABLE_COVERAGE=ON — coverage (GCC/Clang)

### Linux CI / tests-only

```bash
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

The CI workflow (.github/workflows/ci.yml) demonstrates this configuration with optional sanitizers and coverage.

### Fedora 43/44 KDE Wayland build

Install the required Fedora development packages first:

```bash
sudo dnf install cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtbase-private-devel \
  qt6-qtmultimedia-devel qt6-qtsvg-devel \
  qt6-qtdeclarative-devel qt6-qtwayland \
  glibc-devel
```

Then configure, build, and run from the repository root:

```bash
cmake -S native/qt6 -B build-linux-recovery-native-qt6 -G Ninja -DBUILD_APP=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux-recovery-native-qt6 -j 2
rm -f build-linux-recovery-native-qt6/qt.conf  # remove stale files from older configure attempts
./build-linux-recovery-native-qt6/kassetmanagerqt
```

Direct build-tree runs rely on Fedora's system Qt plugin directory. If the app fails with `Could not find the Qt platform plugin "wayland" in ""`, remove a stale `build-linux-recovery-native-qt6/qt.conf` file and rerun the app. That `qt.conf` belongs only in installed/package layouts, not beside the build-tree executable.

If the build stops with a message like `ninja: error: '/usr/lib64/libpng.so', needed by 'kassetmanagerqt', missing and no known rule to make it`, install `libpng-devel` or verify that the bundled tlRender/OpenImageIO CMake metadata points at the bundled static archives under `third_party/tlRender-install-Release/lib/` instead of hard-coded system linker files.

Notes:

- This Linux port uses the active Qt 6 native app build under `native/qt6`
- The branch-local tlRender install is expected at `third_party/tlRender-install-Release`
- Video playback, timeline scrubbing, hover scrubbing, and thumbnails are working in the current Fedora/AppImage baseline
- AppImage video thumbnails use external `/usr/bin/ffmpeg` extraction for robust thumbnail generation
- Live Wayland/tlRender raster video color matches VLC/ffmpeg thumbnails after corrected YUV chroma scaling
- `mpv` should not be treated as the active Linux playback backend in this runtime

### Linux AppImage packaging

For detailed step-by-step instructions, troubleshooting, and common failure modes, see: [docs/APPIMAGE_CREATION.md](../docs/APPIMAGE_CREATION.md)

Quick start:

```bash
# Full build (recommended for first build)
rm -rf /home/npittas/KAssetManager/build-linux-appimage
./scripts/build-linux-appimage.sh
./scripts/package-appimage.sh

# Result: KAssetManager-2.0-x86_64.AppImage
```

Run AppImage:

```bash
chmod +x ./KAssetManager-2.0-x86_64.AppImage
./KAssetManager-2.0-x86_64.AppImage
```

Key requirements:
- **CMAKE_INSTALL_LIBDIR=lib**: Fedora defaults to `lib64`, but must use `lib` to match tlRender's library layout
- **Clean build directory**: Stale caches from git worktrees can cause path issues
- **Recreate qt.conf**: CMake's post-build deletes it; must recreate before `cmake --install`

Packaging notes:

- The repo now contains Linux AppImage helper scripts and packaging metadata
- If host packages for `appimagetool` or `linuxdeploy` are unavailable, official upstream AppImage builds of those tools can be used locally
- If default appimagetool discovery fails, set `APPIMAGETOOL=/home/npittas/KAssetManager/tools/appimage/appimagetool-x86_64.AppImage`
- The packaged Linux runtime uses a writable per-user Qt data location when the executable/AppImage mount is not writable
- Set `LINUXDEPLOY=""` if linuxdeploy-plugin-qt fails (uses qtpaths fallback)

### Linux writable data location

- Portable/dev layout: if `applicationDirPath()/data` is writable, the app keeps using it
- Packaged/AppImage layout: runtime data falls back to a per-user Qt writable data directory
- This applies to:
  - `kasset.db`
  - `projects.db`
  - `app.log`
  - thumbnail cache defaults
  - version storage

### Database and user data location
- Data persists across updates and is stored under QStandardPaths::AppDataLocation
  - Windows example: %AppData%/KAssetManagerQt (exact path may vary by user/profile)
  - Linux/AppImage example: `~/.local/share/<Organization>/<Application>/` as resolved by Qt

### Local validation corpus

The Linux playback validation corpus used during the Fedora 43 Wayland port is under:

```text
/mnt/ssd2/Tests/
```

Video playback checks were concentrated under:

```text
/mnt/ssd2/Tests/Videos/
```

See `docs/linux-wayland-validation.md` for the accepted playback set and AppImage validation notes.

### Troubleshooting
- Missing Qt tools (moc/rcc/windeployqt): Ensure your Qt MSVC bin is on PATH; the build script adds it automatically.
- Missing DLLs on first run: The packaging step copies vcpkg, tlRender, and optional FFmpeg/ImageMagick DLLs into dist/portable/bin.
- FFmpeg not detected (Convert dialog/tools): Set FFMPEG_ROOT to a prefix with include/ and lib/; DLLs will be copied from FFMPEG_ROOT/bin.
- ImageMagick not detected: Set IMAGEMAGICK_ROOT to the portable root that contains magick.exe (either in root or in bin/).
- OpenImageIO (advanced formats): Provided via vcpkg when available; optional at build time.
- Linux AppImage DB startup failures: packaged builds must not write under the mounted AppImage path; current builds use per-user writable Qt data paths.
- Linux/AppImage video preview: playback, scrubbing, thumbnail extraction, and Wayland/tlRender raster color matching are validated in the current release baseline.
