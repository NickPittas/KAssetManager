# KAsset Manager Qt6 Frontend (Skeleton)

## One-command build (Windows)

PowerShell, from repo root (e:\KAssetManager):

```powershell
# Try auto-detect Qt under C:\Qt; override with -QtPrefix if needed
./scripts/build-windows.ps1 -Generator Ninja

# Create NSIS installer (requires NSIS installed)
./scripts/build-windows.ps1 -Generator Ninja -Package
```

Alternatively, use CMakePresets:

```powershell
cmake --preset ninja-release
cmake --build native/qt6/build/ninja-release -j
cpack -G NSIS -C Release -B native/qt6/build/ninja-release
```

This is the initial Qt 6 (Qt Quick/QML + C++) skeleton for the refactor.

## Prerequisites

- Qt 6.7+ (MSVC), CMake 3.21+, Ninja or MSBuild
- Ensure CMAKE_PREFIX_PATH points to your Qt kit (e.g., C:/Qt/6.7.2/msvc2022_64)
- (Later) vcpkg for OpenImageIO/OpenEXR stack

## Build (example)

```powershell
# Set your Qt install path (example):
$env:Qt6="C:/Qt/6.7.2/msvc2022_64"

cmake -S native/qt6 -B build-qt -G "Ninja" -DCMAKE_PREFIX_PATH=$env:Qt6
cmake --build build-qt -j
```

Run the produced executable `kassetmanagerqt`.

## Notes

- Test buttons:
  - CF_HDROP drag-out (real file)
  - Virtual-file drag (1 or 2 files) via FILEDESCRIPTOR/CONTENTS
  - Minimal video player (Qt Multimedia): play/pause/scrub
- Drag-in from Explorer is supported (DropArea), prints acceptance in logs.
- See QT6-REFACTOR-PLAN.md for milestones and the verification matrix.
