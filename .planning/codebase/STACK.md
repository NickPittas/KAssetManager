# Technology Stack

**Analysis Date:** 2026-04-11

## Languages

**Primary:**
- C++20 - main desktop application code in `native/qt6/src/*.cpp`, `native/qt6/src/*.h`, with the standard set in `native/qt6/CMakeLists.txt`.

**Secondary:**
- CMake - build, packaging, dependency discovery, and install rules in `native/qt6/CMakeLists.txt`, `native/qt6/tests/CMakeLists.txt`, and `wayland-probe/CMakeLists.txt`.
- PowerShell - Windows build, packaging, and dependency bootstrap scripts in `scripts/build-windows.ps1`, `scripts/build-installer.ps1`, `scripts/fetch-ffmpeg.ps1`, and `scripts/download-everything-sdk.ps1`.
- YAML - CI automation in `.github/workflows/ci.yml`.

## Runtime

**Environment:**
- Native desktop runtime on Qt 6 Widgets, launched from `native/qt6/src/main.cpp`.
- Windows is the primary packaged target in `README.md`, `scripts/build-windows.ps1`, and `scripts/build-installer.ps1`.
- Linux compatibility code exists in `native/qt6/src/main.cpp`, `native/qt6/src/platform_session.h`, and `wayland-probe/CMakeLists.txt`.

**Package Manager:**
- CMake-based native build in `native/qt6/CMakeLists.txt`.
- vcpkg toolchain support is wired through `native/qt6/CMakeLists.txt` and hardcoded to `C:/vcpkg` in `scripts/build-windows.ps1`.
- Lockfile: missing for native dependencies; no `package-lock.json`, `pnpm-lock.yaml`, `Cargo.lock`, `poetry.lock`, or similar manifest lockfile was detected in this worktree.

## Frameworks

**Core:**
- Qt 6 Widgets/Multimedia/Sql/Concurrent/Svg/OpenGLWidgets - app UI, media UI plumbing, SQLite access, concurrency, and rendering in `native/qt6/CMakeLists.txt`.
- Qt PDF (optional) - PDF viewing when available, guarded in `native/qt6/CMakeLists.txt` with `Qt6::Pdf` and `Qt6::PdfWidgets`.
- ActiveQt (optional, Windows only) - Office automation support detection in `native/qt6/CMakeLists.txt`.

**Testing:**
- QtTest - unit test runner and assertions configured in `native/qt6/tests/CMakeLists.txt`.

**Build/Dev:**
- CMake >= 3.21 - minimum build system version in `native/qt6/CMakeLists.txt`, `native/qt6/CMakePresets.json`, and `wayland-probe/CMakeLists.txt`.
- Ninja and Visual Studio 2022 generators - supported in `native/qt6/CMakePresets.json` and `scripts/build-windows.ps1`.
- clang-tidy, ASan, UBSan, and coverage flags - optional quality tooling in `native/qt6/CMakeLists.txt` and exercised in `.github/workflows/ci.yml`.
- CPack - archive/installer packaging in `native/qt6/CMakeLists.txt`.
- NSIS - Windows installer build path in `scripts/build-installer.ps1` and `native/qt6/CMakeLists.txt`.

## Key Dependencies

**Critical:**
- Qt 6.6.3 in CI - installed by `.github/workflows/ci.yml`; required application modules are resolved in `native/qt6/CMakeLists.txt`.
- tlRender - required package for app builds in `native/qt6/CMakeLists.txt`; linked through `tlRender::tlTimeline`, `tlRender::tlGL`, `tlRender::tlIO`, `tlRender::tlCore`, `tlRender::tlUI`, `tlRender::tlDevice`, `tlRender::tlQt`, and `tlRender::tlQtWidget`.
- SQLite via Qt SQL - persistent asset database in `native/qt6/src/db.cpp` and linked through `Qt6::Sql` in `native/qt6/CMakeLists.txt`.
- minizip-ng - OOXML ZIP reading for Office previews in `native/qt6/CMakeLists.txt` and `native/qt6/src/office_preview.cpp`.

**Infrastructure:**
- OpenImageIO - still-image loading path through `native/qt6/src/oiio_image_loader.cpp`, `native/qt6/src/live_preview_manager.cpp`, and `HAVE_OPENIMAGEIO` definitions from `native/qt6/CMakeLists.txt`.
- OpenColorIO / bundled ACES config - color-management support via tlRender and installed config assets in `native/qt6/CMakeLists.txt`.
- FFmpeg - media conversion, metadata probing, and optional runtime decode support in `native/qt6/src/media_convert_dialog.cpp`, `native/qt6/src/media_converter_worker.cpp`, `native/qt6/src/video_metadata.cpp`, and `native/qt6/src/live_preview_manager.cpp`.
- Everything SDK - Windows fast search integration via `native/qt6/src/everything_search.cpp` and packaged DLL handling in `native/qt6/CMakeLists.txt` and `scripts/build-windows.ps1`.
- ImageMagick - single-image conversion backend in `native/qt6/src/media_convert_dialog.cpp` and `native/qt6/src/media_converter_worker.cpp`.

## Configuration

**Environment:**
- Build-time tool discovery uses environment variables `VCPKG_ROOT`, `FFMPEG_ROOT`, `IMAGEMAGICK_ROOT`, `MAGICK_ROOT`, `Qt6`, `Qt6_DIR`, and `QT_DIR` in `native/qt6/CMakeLists.txt`, `native/qt6/CMakePresets.json`, `scripts/build-windows.ps1`, and `native/qt6/src/media_convert_dialog.cpp`.
- Runtime app identity is configured through `QCoreApplication::setOrganizationName`, `setOrganizationDomain`, `setApplicationName`, and `setApplicationVersion` in `native/qt6/src/main.cpp`.
- Persistent user data uses `QStandardPaths::AppDataLocation` for `kasset.db`, crash files, and thumbnail cache in `native/qt6/src/main.cpp` and `native/qt6/src/thumbnail_cache_manager.cpp`.
- No `.env` or `.env.*` files were detected in `/home/npittas/KAssetManager/.worktrees/linux-port-fedora43-wt`.

**Build:**
- Primary build config: `native/qt6/CMakeLists.txt`.
- Build presets: `native/qt6/CMakePresets.json`.
- Test build config: `native/qt6/tests/CMakeLists.txt`.
- Windows packaging script: `scripts/build-windows.ps1`.
- Installer script: `scripts/build-installer.ps1`.
- CI config: `.github/workflows/ci.yml`.

## Platform Requirements

**Development:**
- CMake 3.21+ and a C++20 compiler per `native/qt6/CMakeLists.txt`.
- Qt 6 with Widgets, Multimedia, MultimediaWidgets, Sql, Concurrent, Svg, SvgWidgets, and OpenGLWidgets available to CMake in `native/qt6/CMakeLists.txt`.
- For Windows packaging, the repository expects `C:/vcpkg`, Qt MSVC kits, optional `third_party/ffmpeg`, optional `third_party/imagemagick`, optional `third_party/everything/Everything64.dll`, and required tlRender under `third_party/tlRender-build/install-Release` or `third_party/tlRender-install-Release` as referenced by `scripts/build-windows.ps1` and `native/qt6/CMakeLists.txt`.
- CI installs Ninja plus Qt 6.6.3 on Windows and Ubuntu in `.github/workflows/ci.yml`.

**Production:**
- Primary deployment target is a packaged Windows desktop app in `dist/portable/bin/kassetmanagerqt.exe`, with ZIP or NSIS packaging from `native/qt6/CMakeLists.txt` and `scripts/build-installer.ps1`.
- Non-Windows packaging currently resolves to TGZ in `native/qt6/CMakeLists.txt`.

---

*Stack analysis: 2026-04-11*
