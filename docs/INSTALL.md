## Installation and Build Guide

This guide covers building, packaging, and running KAsset Manager on Windows (full application) and Linux (tests-only).

### Supported platforms
- Windows 10/11 (64-bit) — full application
- Linux (Ubuntu-latest) — CI/tests-only configuration

### Prerequisites (Windows)
- Visual Studio 2022 or Build Tools (x64)
- Qt 6 (MSVC x64). The build script auto-detects common Qt installs.
- CMake 3.21+ and Ninja (optional; script can build VS or Ninja)
- vcpkg installed at C:\vcpkg (recommended for smoother DLL resolution)
- Optional runtimes (for conversions and advanced formats):
  - FFmpeg (set FFMPEG_ROOT to your build/prefix; used only by the Convert dialog/tools, not for live playback)
  - ImageMagick portable (set IMAGEMAGICK_ROOT; used for single-image conversions)
  - OpenImageIO via vcpkg (optional; enables EXR/PSD/HDR and other advanced formats in LivePreviewManager)
- GStreamer runtime is bundled from third_party/gstreamer and copied automatically into the portable/installer packages; no extra installation is required when using the build script.

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
- Copies required DLLs (Qt, vcpkg, GStreamer, optional FFmpeg/ImageMagick)
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

### Linux (tests-only)

```bash
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

The CI workflow (.github/workflows/ci.yml) demonstrates this configuration with optional sanitizers and coverage.

### Database and user data location
- Data persists across updates and is stored under QStandardPaths::AppDataLocation
  - Windows example: %AppData%/KAssetManagerQt (exact path may vary by user/profile)

### Troubleshooting
- Missing Qt tools (moc/rcc/windeployqt): Ensure your Qt MSVC bin is on PATH; the build script adds it automatically.
- Missing DLLs on first run: The packaging step copies vcpkg, GStreamer, and optional FFmpeg/ImageMagick DLLs into dist/portable/bin.
- FFmpeg not detected (Convert dialog/tools): Set FFMPEG_ROOT to a prefix with include/ and lib/; DLLs will be copied from FFMPEG_ROOT/bin.
- ImageMagick not detected: Set IMAGEMAGICK_ROOT to the portable root that contains magick.exe (either in root or in bin/).
- OpenImageIO (advanced formats): Provided via vcpkg when available; optional at build time.

