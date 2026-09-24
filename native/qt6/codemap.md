# native/qt6/

<!-- Explorer: Fill in this section with architectural understanding -->

## Responsibility

<!-- What is this folder's job in the system? -->

## Design

<!-- Key patterns, abstractions, architectural decisions -->

## Flow

<!-- How does data/control flow through this module? -->

## Integration

<!-- How does it connect to other parts of the system? -->
# native/qt6/

## Responsibility
Defines the Qt 6 application build, platform dependency discovery, and unit-test target composition for the native desktop client.

## Build Design
- `CMakeLists.txt` is the authoritative application manifest.
- `tlRender` is required for app builds and is discovered from branch-local `third_party` install paths.
- Linux and Windows dependency handling diverge in build scripts, but the app target is always driven from this CMake project.
- Tests are declared as individual QtTest executables in `tests/CMakeLists.txt`.

## Verified Branch-Specific Behavior
- This branch extends tlRender candidate paths to include Linux-style install layouts such as `third_party/tlRender-install-Release`.
- The app still links Qt Widgets, Multimedia, SQL, Concurrent, SVG, and OpenGLWidgets.
- Unit tests intentionally compile some targets with feature defines forced to `0`, so application code must use `#if defined(X) && X` style guards.

## Control Flow
1. Configure locates Qt and tlRender.
2. `kassetmanagerqt` target compiles the monolithic UI shell plus models, DB, preview, and media layers.
3. Test targets compile selected source files directly from `src/` and run from the install tree.

## Integration
- Consumed by: local developer builds, CI, Windows packaging scripts
- Depends on: tlRender install, Qt 6, minizip-ng, bundled FFmpeg via tlRender
