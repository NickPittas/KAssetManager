# KAsset Manager 1.8.0 Release Notes

Release Date: 2026-01-13

Overview
--------
Version 1.8.0 transitions KAsset Manager's playback pipeline to tlRender (mrv2's engine),
bringing OCIO-aware video and image-sequence playback while retiring the legacy GStreamer
preview path. The release also aligns build, portable, and installer metadata with the
1.8.0 version.

Highlights
----------
- tlRender (mrv2) playback engine now powers video and image-sequence previews
- OCIO color management available throughout the playback pipeline
- Legacy GStreamer preview runtime removed from packaging
- Build and installer metadata updated to version 1.8.0

User-Facing Changes
-------------------
- Video and image-sequence playback now uses tlRender for consistent results across formats.
- GStreamer is no longer bundled or required for preview playback.
- Application versioning in build outputs and installers reflects 1.8.0.

Developer/Technical Notes
-------------------------
- CMake project version is set to `1.8.0` and Windows resources/installer metadata match.
- tlRender is consumed via its CMake package and provides bundled OpenColorIO/OpenImageIO.
- The build expects a tlRender install at `third_party/tlRender-build/install-Release`
  (see `native/qt6/CMakeLists.txt` for the candidate search paths).

Upgrade & Build
---------------
1. Reconfigure CMake (recommended):

```powershell
cd native/qt6
cmake -S . -B build\ninja -G Ninja
cmake --build build\ninja --target kassetmanagerqt
```

Notes & Known Issues
--------------------
- If you have a custom tlRender build location, ensure `TLRENDER_ROOT` or the expected
  install path is available before configuring.

Acknowledgements
----------------
Thanks to contributors and testers who helped validate the tlRender playback migration.
