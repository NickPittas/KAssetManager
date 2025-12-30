# KAsset Manager 1.6.0 Release Notes

Release Date: 2025-12-30

Overview
--------
Version 1.6.0 is a focused quality and UX release that completes the dual-pane File Manager experience, fixes preview navigation bugs, and polishes UI consistency. It also includes a compatibility fix for video profile constants with modern FFmpeg headers.

Highlights
----------
- Dual-pane File Manager: persistent state across restarts (enabled/disabled and pane sizes)
- Full-screen preview: navigation now follows the pane that opened the overlay (primary or secondary)
- Folder tree: horizontal auto-scroll suppressed — scrollbar will remain at left edge
- UI consistency: toolbar heights and button sizes standardized
- FFmpeg compatibility updates for video profile constants

User-Facing Changes
-------------------
- Dual-pane mode remembered: If you enable dual-pane mode and arrange the panes, the app will restore that layout on next launch.
- Preview navigation: If you open the full-screen preview from the second/secondary pane, using arrow keys or scroll will navigate the secondary pane's file list (no longer the primary pane).
- Tree scrolling: Very long folder names will no longer cause the tree view to auto-scroll horizontally. The horizontal scroll position remains at the left edge.
- Toolbar appearance: Tool buttons are smaller and consistent across toolbars; toggle state shows a clear checked appearance.

Developer/Technical Notes
-------------------------
- CMake project version updated to `1.6.0`. The `KAM_APP_VERSION` compile definition and Windows resources have been updated accordingly.
- Video metadata parsing (`video_metadata.cpp`) now uses `AV_PROFILE_*` constants for compatibility with newer FFmpeg versions (v61+ headers in vcpkg).
- `mainwindow.cpp` now persists dual-pane splitter sizes under `FileManager/DualPaneSplitterSizes` and restores them on startup.

Upgrade & Build
---------------
1. Reconfigure CMake (recommended):

```powershell
cd native/qt6
cmake -S . -B build\ninja -G Ninja
cmake --build build\ninja --target kassetmanagerqt
```

2. On Windows, the Windows resource (`native/qt6/src/app.rc`) is updated — rebuild to get correct EXE version info.

Notes & Known Issues
--------------------
- Some third-party bundled libraries may still report their own versions inside `third_party/` folders; those are unrelated to the app version.
- If you build with different FFmpeg headers than the ones packaged with the repository, ensure the `AV_PROFILE_*` constants exist in your FFmpeg headers.

Acknowledgements
----------------
Thanks to contributors and testers who helped validate dual-pane workflows and preview navigation across panes.
