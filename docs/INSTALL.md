## Installation and Build Guide

This guide covers building, packaging, and running KAsset Manager on Windows and on the current validated Linux baseline: Fedora 43 KDE Wayland.

### Supported platforms
- Windows 10/11 (64-bit) — full application
- Fedora 43 KDE Wayland — validated Linux runtime baseline
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

### Fedora 43 KDE Wayland build

```bash
cmake -S native/qt6 -B build-linux-recovery-native-qt6 -G Ninja -DBUILD_APP=ON -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux-recovery-native-qt6 -j 2
./build-linux-recovery-native-qt6/kassetmanagerqt
```

Notes:

- This Linux port uses the active Qt 6 native app build under `native/qt6`
- The current Linux playback path for demanding MOV files is the rewritten FFmpeg MOV backend in the current worktree
- `mpv` should not be treated as the active Linux playback backend in this runtime

### Linux AppImage packaging

Build AppDir staging:

```bash
./scripts/build-linux-appimage.sh
```

Package AppImage:

```bash
./scripts/package-appimage.sh
```

Run AppImage:

```bash
chmod +x ./KAssetManager-x86_64.AppImage
./KAssetManager-x86_64.AppImage
```

Packaging notes:

- The repo now contains Linux AppImage helper scripts and packaging metadata
- If host packages for `appimagetool` or `linuxdeploy` are unavailable, official upstream AppImage builds of those tools can be used locally
- The packaged Linux runtime uses a writable per-user Qt data location when the executable/AppImage mount is not writable

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
- Linux color controls / OCIO: playback recovery and AppImage startup are validated, but OCIO/color controls on the non-native Linux video playback paths remain follow-up work.
