# Phase 07: Bundle And Verify Converters - Research

**Researched:** 2026-04-14  
**Phase objective:** Prefer bundled `ffmpeg` and `magick` paths in Linux runtime flows and verify that conversion features use those bundled tools before falling back to system `PATH`. [VERIFIED: .planning/ROADMAP.md, .planning/REQUIREMENTS.md, .planning/STATE.md]

## Summary

The external converter pipeline is narrow and easy to trace: File Manager and Asset Manager context menus open `MediaConvertDialog`, the dialog resolves `ffmpeg`/`magick`, selected settings are converted into `MediaConverterWorker::Task` objects, and the worker launches external processes with `QProcess`. [VERIFIED: native/qt6/src/mainwindow.cpp:3090-3106,7193-7199; native/qt6/src/media_convert_dialog.cpp:18-35,401-499; native/qt6/src/media_converter_worker.cpp:69-131]

The Linux-specific problem is not the worker launch itself; it is tool resolution. The current Linux code still prefers plain-name fallback (`"ffmpeg"`, `"magick"`) and several bundled-path checks are Windows-only or `.exe`-only, so a Linux AppImage can appear to work on a developer machine with system packages installed while silently bypassing bundled tools. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:301-364; native/qt6/src/media_converter_worker.cpp:204-239,458-463] [CITED: https://doc.qt.io/qt-6/qprocess.html#finding-the-executable]

**Primary recommendation:** First implement one Linux-only lookup slice: centralize bundled-tool discovery for `ffmpeg`, `ffprobe`, and `magick`, return absolute paths when found, and make validation fail early when only plain-name fallback is available during bundled-runtime verification. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:301-364,401-421; native/qt6/src/media_converter_worker.cpp:204-239,458-463; docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md:433-441]

## Project Constraints (from AGENTS.md)

- Read codemaps first and trace the real runtime/build path before deciding anything. [VERIFIED: AGENTS.md]
- Base findings on present code and observed evidence, not speculation. [VERIFIED: AGENTS.md]
- Fix gatekeeping blockers before downstream polish. [VERIFIED: AGENTS.md]
- Prefer the smallest justified change and re-check affected paths after each fix. [VERIFIED: AGENTS.md]
- Verify results with concrete evidence; do not present speculative fixes as complete. [VERIFIED: AGENTS.md]
- Only write inside the repository root. [VERIFIED: AGENTS.md]

## Traced Runtime Paths

### 1) User entry points into conversion

- File Manager selection opens `MediaConvertDialog` from the context menu when all selected files have supported image/video extensions. [VERIFIED: native/qt6/src/mainwindow.cpp:3080-3106]
- Asset Manager selection opens the same `MediaConvertDialog` from the asset context menu. [VERIFIED: native/qt6/src/mainwindow.cpp:7193-7199]
- No other traced Linux runtime path in `native/qt6/src` launches external `ffmpeg` or `magick` binaries for conversion work. [VERIFIED: grep over native/qt6/src for `ffmpeg|magick|ImageMagick|MediaConvertDialog`]

### 2) Dialog-time tool discovery

- `MediaConvertDialog` constructor immediately calls `locateFfmpeg()` and `locateMagick()` and stores the results in `m_ffmpeg` and `m_magick`. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:18-35]
- `locateFfmpeg()` on Linux checks `applicationDirPath()/ffmpeg`, then `../../third_party/ffmpeg/bin/ffmpeg`, then `FFMPEG_ROOT/bin/ffmpeg`, and otherwise returns the plain program name `ffmpeg`. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:301-321]
- `locateMagick()` is effectively Windows-shaped: its next-to-app check is guarded by `#ifdef Q_OS_WIN`, its `third_party` probes only look for `magick.exe`, and its environment-variable probes only look for `magick.exe`; otherwise it returns the plain program name `magick`. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:326-364]
- On Unix, `QProcess` searches `PATH` when the program is only a plain file name, and Qt recommends passing an absolute executable path to avoid platform-dependent lookup behavior. [CITED: https://doc.qt.io/qt-6/qprocess.html#finding-the-executable]

### 3) Task validation and worker execution

- `validateAndBuildTasks()` only rejects missing tools when `m_ffmpeg` or `m_magick` is empty, but the locator functions return plain names instead of empty strings on fallback, so validation does not distinguish bundled lookup success from `PATH` fallback. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:321,364,401-421]
- `onStart()` passes the resolved strings directly into `MediaConverterWorker` with `setFfmpegPath()` and `setMagickPath()`. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:471-499; native/qt6/src/media_converter_worker.h:59-60]
- `MediaConverterWorker::buildCommand()` uses ImageMagick only for single-image output targets and FFmpeg for video and sequence outputs, then launches the chosen program through `QProcess::setProgram()` and `setArguments()`. [VERIFIED: native/qt6/src/media_converter_worker.cpp:121-131,327-519]

### 4) FFprobe-dependent sub-paths inside the worker

- Duration, width/height, average FPS, and MOV alpha-preservation probing all derive `ffprobe` from the selected FFmpeg directory, but they look for `ffprobe.exe` specifically and otherwise fall back to plain `ffprobe` on `PATH`. [VERIFIED: native/qt6/src/media_converter_worker.cpp:204-239,458-463]
- That means Linux can launch a bundled `ffmpeg` binary while still using a system `ffprobe`, or fail to find bundled `ffprobe` even when it sits next to bundled `ffmpeg` as `ffprobe` without `.exe`. [VERIFIED: native/qt6/src/media_converter_worker.cpp:208-209,234-235,459-460]

### 5) Packaging/install state relevant to Phase 07

- The current non-Windows CMake install section installs the app binary, icons, and bundled OCIO config, but the visible converter-install logic is Windows-only; no traced Linux install rule currently stages `ffmpeg`, `ffprobe`, or `magick`. [VERIFIED: native/qt6/CMakeLists.txt:487-599]
- The phase-planning notes already expect a Linux slice that prefers bundled AppImage runtime paths first and validates MP4/MOV/PNG/TIF conversion behavior afterward. [VERIFIED: docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md:428-441]
- The repository currently contains Windows-style bundled converter assets under `third_party/ffmpeg/bin/ffmpeg.exe` and `ffprobe.exe`, while the checked `third_party/ImageMagick-7.1.2-8-portable-Q16-x64` directory did not expose a Linux `magick` binary in this session. [VERIFIED: glob of `third_party/ffmpeg/**/*`; glob of `third_party/ImageMagick*/*`; local `ls` of `third_party/ffmpeg/bin`]

### 6) Scope boundary: preview/thumbnail runtime is different

- Video preview and thumbnail flows do not go through `MediaConvertDialog`; thumbnail generation uses `TLRenderPlayer` helpers and image loaders, and `live_preview_manager.cpp` uses linked FFmpeg libraries when compiled with `HAVE_FFMPEG`, not external CLI binaries. [VERIFIED: native/qt6/src/thumbnail_generator_worker.cpp:217-271; native/qt6/src/live_preview_manager.cpp:26-34,108-120]
- Phase 07 should therefore focus on explicit conversion/export runtime paths, not on tlRender playback or linked-FFmpeg preview code. [VERIFIED: traced code paths above]

## Linux-Specific Risk Areas

- **Bundled-path bypass by system packages:** this machine already has `/usr/bin/ffmpeg`, `/usr/bin/ffprobe`, and `/usr/bin/magick`, so current Linux testing can pass even if bundled lookup is wrong. [VERIFIED: local shell `command -v ffmpeg`, `ffprobe`, `magick`]
- **ImageMagick lookup is not Linux-bundled-aware:** Linux never checks `applicationDirPath()/magick`, `../bin/magick`, or other non-`.exe` bundled candidates before falling back to `PATH`. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:326-364]
- **FFprobe sibling lookup is Windows-only:** hard-coded `ffprobe.exe` makes Linux metadata/progress/alpha probing miss bundled `ffprobe` unless the environment `PATH` happens to rescue it. [VERIFIED: native/qt6/src/media_converter_worker.cpp:208-209,234-235,459-460]
- **Validation cannot prove bundling today:** because the locator fallback returns plain names instead of “not found” or “PATH fallback,” the UI cannot tell the user whether it resolved a bundled binary or a system binary. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:321,364,401-421]
- **Phase 08 packaging depends on this phase:** AppImage bundling cannot be trusted until Phase 07 establishes where Linux runtime lookup expects the tools to live. [VERIFIED: .planning/ROADMAP.md:72-80; docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md:450-470]

## Likely Blockers

| Blocker | Evidence | Impact |
|---|---|---|
| Linux `magick` bundled lookup is effectively missing. [VERIFIED: codebase read] | `locateMagick()` only probes `magick.exe`-style paths and skips next-to-app lookup entirely on non-Windows. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:326-364] | Single-image conversions can silently use system `magick` instead of the bundled binary, defeating the phase goal. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:401-421] |
| FFprobe companion lookup is wrong for Linux bundles. [VERIFIED: codebase read] | Worker probes `ffprobe.exe` next to FFmpeg, then drops to `PATH`. [VERIFIED: native/qt6/src/media_converter_worker.cpp:204-239,458-463] | Progress estimation, FPS probing, and MOV alpha detection can diverge from the bundled FFmpeg runtime. [VERIFIED: native/qt6/src/media_converter_worker.cpp:95-115,393,457-480] |
| Current validation does not distinguish absolute bundled paths from `PATH` fallback. [VERIFIED: codebase read] | Both locator functions return plain program names rather than an empty/not-found state. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:321,364] | Linux verification can report false confidence on developer machines with system packages installed. [VERIFIED: local shell tool availability; native/qt6/src/media_convert_dialog.cpp:401-421] |
| Linux install/AppDir staging for converters is not in the traced CMake path yet. [VERIFIED: codebase read] | Non-Windows install logic shown in `native/qt6/CMakeLists.txt` does not install converter binaries. [VERIFIED: native/qt6/CMakeLists.txt:487-599] | Even perfect runtime lookup code will fail in packaged builds if the binaries are never staged. [VERIFIED: native/qt6/CMakeLists.txt:487-599; docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md:460-477] |
| Automated regression coverage is almost absent for this phase. [VERIFIED: codebase read] | `test_media_converter_worker.cpp` only covers the empty-queue case and does not test Linux locator behavior or probe selection. [VERIFIED: native/qt6/tests/test_media_converter_worker.cpp:1-35] | Phase 07 fixes will be easy to regress during Phase 08 packaging work. [VERIFIED: native/qt6/tests/test_media_converter_worker.cpp:1-35] |

## Recommended First Implementation Slice

1. Extract converter lookup into one shared helper that resolves **absolute** Linux candidates for `ffmpeg`, `ffprobe`, and `magick` from the app runtime layout first, then optional env vars, and only then plain-name developer fallback. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:301-364; native/qt6/src/media_converter_worker.cpp:204-239,458-463] [CITED: https://doc.qt.io/qt-6/qprocess.html#finding-the-executable]
2. Make the helper surface whether the result is **bundled absolute path**, **env absolute path**, or **PATH fallback**, and show that in Phase 07 verification output instead of treating all non-empty strings as equivalent. [VERIFIED: native/qt6/src/media_convert_dialog.cpp:401-421]
3. Update worker-side `ffprobe` lookup to use the resolved sibling path on Linux (`ffprobe`, not only `ffprobe.exe`) so metadata/progress probes stay aligned with the same bundle as `ffmpeg`. [VERIFIED: native/qt6/src/media_converter_worker.cpp:204-239,458-463]
4. Add one focused Linux regression test layer for lookup/probe resolution before broader packaging work: unit-test the resolver with mocked app-dir/env layouts, then perform one manual smoke pass for MP4, MOV, PNG sequence, and single-image conversion using bundled binaries. [VERIFIED: native/qt6/tests/test_media_converter_worker.cpp:1-35; docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md:428-441]

## Sources

- `AGENTS.md` [VERIFIED: read]
- `codemap.md` [VERIFIED: read]
- `.planning/ROADMAP.md` [VERIFIED: read]
- `.planning/STATE.md` [VERIFIED: read]
- `.planning/REQUIREMENTS.md` [VERIFIED: read]
- `native/qt6/src/codemap.md` [VERIFIED: read]
- `native/qt6/src/mainwindow.cpp` [VERIFIED: read]
- `native/qt6/src/media_convert_dialog.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/media_converter_worker.cpp` / `.h` [VERIFIED: read]
- `native/qt6/src/thumbnail_generator_worker.cpp` [VERIFIED: read]
- `native/qt6/src/live_preview_manager.cpp` [VERIFIED: read]
- `native/qt6/CMakeLists.txt` [VERIFIED: read]
- `native/qt6/tests/test_media_converter_worker.cpp` [VERIFIED: read]
- `docs/superpowers/plans/2026-04-10-linux-port-fedora43-wayland-appimage.md` [VERIFIED: read]
- Qt 6 `QProcess` docs, “Finding the Executable” [CITED: https://doc.qt.io/qt-6/qprocess.html#finding-the-executable]
