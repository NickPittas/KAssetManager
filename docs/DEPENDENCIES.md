## Third-party dependencies

This application bundles or integrates with the following third-party tools/libraries:

- FFmpeg
  - Version: 8.0 (www.gyan.dev full build)
  - License: GPL v3
  - Upstream commit: https://github.com/FFmpeg/FFmpeg/commit/140fd653ae
  - Location (if bundled): third_party/ffmpeg
  - Override at configure time via environment variable: set FFMPEG_ROOT to your FFmpeg prefix (containing include/ and lib/)

- ImageMagick
  - Recommended: Latest 7.1.x portable (Q16, x64). Tested with 7.1.2-8.
  - Location (if bundled): third_party/ImageMagick-7.1.2-8-portable-Q16-x64 or third_party/imagemagick
  - Override via IMAGEMAGICK_ROOT (or MAGICK_ROOT) environment variable. The build/packaging scripts will auto-detect common portable folders and copy magick.exe and DLLs into the app runtime. Ghostscript executables are not copied.
  - Security note: We do not use PS/PDF conversion. If you provide your own ImageMagick with Ghostscript delegates enabled, review your delegates.xml and policy.xml to ensure untrusted PS/PDF inputs are disabled.

- OpenImageIO (optional via vcpkg)
  - Enabled when found by CMake (HAVE_OPENIMAGEIO)

- Qt 6 (Widgets, Multimedia, Sql, Concurrent, Svg, optionally Pdf/PdfWidgets)

### Notes
- When using vcpkg, set VCPKG_ROOT to your vcpkg directory before configuring CMake, and optionally VCPKG_TARGET_TRIPLET (defaults to x64-windows).
- To use a custom FFmpeg build, set FFMPEG_ROOT so that $FFMPEG_ROOT/include and $FFMPEG_ROOT/lib exist. DLLs from $FFMPEG_ROOT/bin will be installed alongside the app.
- AddressSanitizer can be enabled during development with -DENABLE_ASAN=ON when using Clang/GCC.

