# Third-party dependencies

The application depends on and/or bundles the following components.

## Qt 6

- Modules: Widgets, Multimedia, MultimediaWidgets, Sql, Concurrent, Svg, SvgWidgets (optionally Pdf/PdfWidgets)

## tlRender (mrv2 playback)

- Location: prebuilt install under `third_party/tlRender-build/install-Release`
- Usage: Primary backend for video and image-sequence playback (PreviewOverlay, LivePreviewManager)
- Packaging: build script and install rules bundle tlRender runtime DLLs into dist/portable and installer packages
- Note: tlRender bundles OpenColorIO/OpenImageIO/FFmpeg dependencies for playback


## FFmpeg

- Version: 8.0 (full build from [Gyan](https://www.gyan.dev))
- Usage: Only used by the Convert dialog/tools for format conversion; live playback is handled by tlRender (bundled FFmpeg).
- License: GPL v3
- Location (if bundled): third_party/ffmpeg
- Configure override: set FFMPEG_ROOT to your FFmpeg prefix (must contain include/, lib/, and bin/)
- Packaging: build script copies DLLs and ffmpeg/ffprobe executables from %FFMPEG_ROOT%/bin into the portable package
- Update procedure:
  1) Download/prepare a newer FFmpeg build
  2) Set FFMPEG_ROOT to point at the new prefix
  3) Rebuild/package; verify in logs that the custom FFmpeg root is detected

## ImageMagick (conversions)

- Recommended: Latest 7.1.x portable (Q16, x64). Tested with 7.1.2-8
- Location (if bundled): third_party/ImageMagick-*/ or third_party/imagemagick
- Configure override: set IMAGEMAGICK_ROOT (or MAGICK_ROOT)
- Packaging: build script auto-detects portable layouts and copies magick.exe and DLLs
- Security note: Ghostscript delegates are not copied; if you use your own ImageMagick, review delegates.xml and policy.xml to disable untrusted PS/PDF inputs

## OpenImageIO (image loading)

- Optional via vcpkg; enabled when found (CMake defines HAVE_OPENIMAGEIO)
- Handles all still image formats (PNG, JPG, BMP, GIF, TIFF, EXR, HDR, PSD, etc.) for consistent quality, proper aspect ratio handling, and unified behavior
- Eliminates redundancy with Qt's image reader and provides superior color management
- When unavailable, Qt's image reader is used as fallback for common formats

## Everything SDK (search integration)

- Runtime DLL: Everything64.dll (copied to bin if present)
- Expected path: third_party/everything/Everything64.dll
- If missing, follow CMake hint or run a helper to download the SDK (see repository scripts/ if provided)

## minizip-ng

- Provided via vcpkg; linked as MINIZIP::minizip-ng

---

## Vulnerability scanning and maintenance

- GitHub Dependabot is enabled for Actions and vcpkg manifests (.github/dependabot.yml)
- Keep FFmpeg/ImageMagick up to date, especially when security advisories are published
- CI uses clang-tidy and optional sanitizers (ASan/UBSan) for extra safety on Linux

## Environment variables summary

- VCPKG_ROOT: vcpkg directory (e.g., C:\\vcpkg)
- VCPKG_TARGET_TRIPLET: defaults to x64-windows
- FFMPEG_ROOT: custom FFmpeg prefix (include/, lib/, bin/; used only by the Convert dialog/tools)
- IMAGEMAGICK_ROOT (or MAGICK_ROOT): portable ImageMagick root folder
