## Developer Guide

This guide summarizes the code structure, build options, testing, and the key engineering decisions (threading, DB, logging, and security fixes).

### Project structure
```
native/qt6/
  src/                 # application sources
  tests/               # QtTest unit tests
  CMakeLists.txt       # build configuration (app + tests)
scripts/
  build-windows.ps1    # Windows build/packaging helper
  build-linux-appimage.sh
  package-appimage.sh
docs/
  ARCHITECTURE.md, DEPENDENCIES.md, INSTALL.md, USER_GUIDE.md
```

### Building
- See INSTALL.md for full instructions
- Key CMake options:
  - BUILD_APP=ON/OFF
  - BUILD_TESTS=ON/OFF
  - ENABLE_ASAN=ON, ENABLE_UBSAN=ON (Clang/GCC)
  - ENABLE_CLANG_TIDY=ON
  - ENABLE_COVERAGE=ON (GCC/Clang)
- Optional / external dependencies:
  - tlRender install under third_party/tlRender-build/install-Release (required for video and image-sequence playback; bundled by the Windows build scripts).
  - FFmpeg via FFMPEG_ROOT or vcpkg (headers/libs; used only by the Convert dialog/tools, not for live playback).
  - OpenImageIO via vcpkg (HAVE_OPENIMAGEIO automatically defined when found).
  - ImageMagick portable via IMAGEMAGICK_ROOT (only used at runtime for conversions).

### Linux baseline
- Fedora 43 KDE Wayland is the current validated Linux runtime baseline.
- The active Linux playback work in this branch uses:
  - tlRender where still applicable
  - a rewritten FFmpeg MOV player path for demanding MOV playback on Wayland
- `mpv` should be treated as inactive/stubbed for this Linux runtime path.

### Linux packaging
- Linux packaging currently targets AppImage first.
- AppDir/AppImage helpers live in:
  - `scripts/build-linux-appimage.sh`
  - `scripts/package-appimage.sh`
  - `native/qt6/packaging/linux/`
- The current AppImage runtime is expected to use a per-user writable Qt data root when the executable directory is not writable.

### Testing
- Unit tests use QtTest. Build with -DBUILD_TESTS=ON
- Run locally:
  - Windows: after install step, test binaries in native/qt6/build/<gen>/install_run/bin
  - Linux: ctest --test-dir build --output-on-failure
  - Fedora 43 Wayland app runtime: `./build-linux-recovery-native-qt6/kassetmanagerqt`
  - Linux AppImage runtime: `./KAssetManager-x86_64.AppImage`
- Notes:
  - Some tests disable heavy features by compiling with definitions set to 0 and using guards `#if defined(HAVE_...) && HAVE_...` in code
  - Prefer deterministic test cases (QSignalSpy with bounded wait)
- Coverage:
  - GCC/Clang: -DENABLE_COVERAGE=ON, then run tests and generate report with gcovr (see CI job coverage-ubuntu)

### Logging
- A single message handler funnels Qt logs to LogManager
- LogManager is thread-safe, keeps a ring buffer (~1000 entries), and writes to a writable runtime log path
- On packaged/AppImage Linux runs, the writable log path must not resolve inside the mounted AppImage
- Avoid logging sensitive data. File paths and error summaries are acceptable

### Database patterns
- SQLite via QtSql (QSqlDatabase/QSqlQuery)
- Connection is not thread-safe across threads; use the connection from the thread that opened it
- Heavy DB operations are batched in transactions
- Prepared statements and IN-clauses are used for bulk operations; placeholder strings are built safely
- Schema versioning via PRAGMA user_version; migrations performed on startup as needed
- Linux/AppImage runtime data should resolve through the shared runtime-path helper rather than writing into the mounted application directory

### Threading and I/O
- UI thread must remain responsive; no blocking I/O on UI
- Use QtConcurrent/QThreadPool for background work
- Live preview decoding runs off-UI with LRU QCache for pixmaps
- Importer performs DB work in batches and signals progress
- Avoid QApplication::processEvents(); communicate with signals/slots (queued connections)

### Live preview and conversions
- tlRender powers video and image-sequence playback; OpenImageIO handles all still image formats for consistent quality and behavior; FFmpeg is no longer used for playback.
- On the current Fedora 43 Wayland branch, demanding MOV playback uses the rewritten FFmpeg MOV player path rather than relying on `mpv`.
- RAII wrappers ensure FFmpeg resources used by the Convert dialog are released on all paths.
- Image conversions use ImageMagick; video conversions use FFmpeg via the Convert dialog/tools.
- Conversion Pause/Resume is disabled by design.

### Linux validation corpus
- Local Linux/Wayland playback validation was run against `/mnt/ssd2/Tests/`, especially `/mnt/ssd2/Tests/Videos/`.
- See `docs/linux-wayland-validation.md` for the accepted playback set and current Linux/AppImage notes.

### Security & hardening (summary)
See CODEBASE_REVIEW_REPORT.md for the detailed audit. Implemented highlights:
- SQL injection fixes with whitelisting and prepared statements
- Hardened external tool invocations (avoid flag injection; safe path handling)
- Path traversal mitigation in rename/move
- Controlled crash log information (Release avoids leaking addresses)
- Resource safety: RAII for FFmpeg; OIIO cleanup paths audited
- Safer string handling, bounds checks, and removal of magic numbers

### CI
- Windows and Ubuntu CI build tests from .github/workflows/ci.yml
- Ubuntu jobs run with optional sanitizers and a dedicated coverage job
- Dependabot is configured for workflow and dependency scanning

### Contributing
- Follow clang-tidy rules in .clang-tidy
- Prefer Qt containers (QString, QVector, QHash) and RAII for resources
- Keep blocking I/O off the UI thread; validate inputs; use transactions for DB batches
- Add or update tests for new behavior; enable coverage locally if possible
