# Coding Conventions

**Analysis Date:** 2026-04-11

## Naming Patterns

**Files:**
- Use lowercase snake_case for source and header filenames in `native/qt6/src/` and `native/qt6/tests/`: `native/qt6/src/live_preview_manager.cpp`, `native/qt6/src/file_manager_pane.h`, `native/qt6/tests/test_live_preview_manager.cpp`.
- Use `test_*.cpp` for QtTest executables in `native/qt6/tests/`: `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_sequence_detector.cpp`.

**Functions:**
- Use lowerCamelCase for free functions, methods, slots, and helpers: `DB::ensureRootFolder()` in `native/qt6/src/db.cpp`, `AssetsModel::setFolderId()` in `native/qt6/src/assets_model.cpp`, `GridScrubController::canScrubFile()` in `native/qt6/src/grid_scrub.cpp`.
- Use `test*` names for test slots: `TestSequenceDetector::testDetectSequences_basic()` in `native/qt6/tests/test_sequence_detector.cpp`, `TestLivePreviewManager::testRequestAndCacheStillPng()` in `native/qt6/tests/test_live_preview_manager.cpp`.

**Variables:**
- Use `m_` prefix for member state: `m_folderId`, `m_reloadTimer` in `native/qt6/src/assets_model.h`; `m_cache`, `m_decodePool` in `native/qt6/src/live_preview_manager.h`.
- Use `k`-prefixed constants for named constants: `kLatestVersion` in `native/qt6/src/db.cpp`, `kMinCacheEntries` in `native/qt6/src/live_preview_manager.cpp`, `kScrubDefaultPosition` in `native/qt6/src/grid_scrub.cpp`.
- Use `s_` for file-static shared state: `s_durationCache` in `native/qt6/src/live_preview_manager.cpp`.

**Types:**
- Use PascalCase for classes, structs, and enums: `FileManagerPane` in `native/qt6/src/file_manager_pane.h`, `AssetRow` in `native/qt6/src/assets_model.h`, `SequenceMeta` in `native/qt6/src/live_preview_manager.h`, `TypeFilter` in `native/qt6/src/assets_model.h`.
- Use namespace-scoped helpers when the type is utility-only: `namespace PlatformSession` in `native/qt6/src/platform_session.h`.

## Code Style

**Formatting:**
- No dedicated formatter config is detected in the workspace root; `.clang-format` and `.editorconfig` are not present.
- Follow the existing manual Qt/C++ style in `native/qt6/src/`: braces on the next line for function definitions in `native/qt6/src/main.cpp` and `native/qt6/src/grid_scrub.cpp`, inline short methods in headers such as `AssetsModel::rowCount()` in `native/qt6/src/assets_model.h`, and spaces around references/pointers like `const QString&` and `QObject* parent`.
- Prefer Qt string/value helpers already used across the codebase: `QStringLiteral(...)` in `native/qt6/src/main.cpp`, `QLatin1Char('0')` in `native/qt6/src/assets_model.cpp`, `Q_UNUSED(...)` in `native/qt6/src/assets_model.h` and `native/qt6/src/mainwindow.cpp`.

**Linting:**
- Static analysis is configured with `.clang-tidy`.
- Enabled checks: `clang-analyzer-*`, `bugprone-*`, `performance-*`, and `modernize-*` in `.clang-tidy`.
- `ENABLE_CLANG_TIDY` is an opt-in CMake flag in `native/qt6/CMakeLists.txt`; CI enables it in `.github/workflows/ci.yml`.

## Import Organization

**Order:**
1. Qt and platform headers first: `#include <QApplication>`, `#include <QDebug>` in `native/qt6/src/main.cpp`.
2. Local project headers next: `#include "mainwindow.h"`, `#include "db.h"` in `native/qt6/src/main.cpp`.
3. Standard library or conditional external headers last/near usage: `#include <iostream>` in `native/qt6/src/main.cpp`, FFmpeg headers behind feature guards in `native/qt6/src/live_preview_manager.cpp`.

**Path Aliases:**
- Not detected. Use direct relative includes for project code such as `#include "db.h"` in `native/qt6/src/assets_model.cpp` and `#include "../src/live_preview_manager.h"` in `native/qt6/tests/test_live_preview_manager.cpp`.

## Error Handling

**Patterns:**
- Prefer early returns on invalid state or failures: `DB::init()` in `native/qt6/src/db.cpp`, `Importer::importFile()` in `native/qt6/src/importer.cpp`, `LivePreviewManager::requestFrame()` in `native/qt6/src/live_preview_manager.cpp`.
- Return `bool`, `int`, or empty Qt value types to signal failure instead of throwing: `DB::exec()` in `native/qt6/src/db.cpp`, `Importer::importFolder()` in `native/qt6/src/importer.cpp`, `LivePreviewManager::cachedFrame()` in `native/qt6/src/live_preview_manager.cpp`.
- Use log-based error reporting with `qWarning()`/`qCritical()` for operational failures: `native/qt6/src/db.cpp`, `native/qt6/src/main.cpp`, `native/qt6/src/preview_overlay.cpp`.
- Exceptions are not a primary error flow. `native/qt6/src/live_preview_manager.cpp` includes `<stdexcept>`, but observed control flow still uses return values and emitted failure signals.

## Logging

**Framework:** `qDebug` / `qWarning` / `qCritical` routed through `customMessageHandler` and `LogManager`.

**Patterns:**
- Keep the single Qt message handler in `native/qt6/src/main.cpp`; do not add another handler.
- Use structured component prefixes in log messages: `"[MAIN]"` in `native/qt6/src/main.cpp`, `"[LivePreview]"` in `native/qt6/src/live_preview_manager.cpp`, `"[PreviewOverlay]"` in `native/qt6/src/preview_overlay.cpp`.
- Use `LogManager::instance().addLog(...)` for persistent application log entries in `native/qt6/src/main.cpp` and `native/qt6/src/importer.cpp`.

## Comments

**When to Comment:**
- Add doc blocks for non-trivial widgets/managers and public APIs: `FileManagerPane` in `native/qt6/src/file_manager_pane.h`, `LivePreviewManager` in `native/qt6/src/live_preview_manager.h`, `GridScrubOverlay` in `native/qt6/src/grid_scrub.h`.
- Use inline rationale comments for performance, threading, and platform constraints: `native/qt6/src/live_preview_manager.cpp`, `native/qt6/src/importer.cpp`, `native/qt6/src/main.cpp`.

**JSDoc/TSDoc:**
- Not applicable.
- Use C++ Doxygen-style `/** ... */` blocks instead, as in `native/qt6/src/file_manager_pane.h` and `native/qt6/src/media/tlrender_viewport.h`.

## Function Design

**Size:**
- Small helpers are preferred for reusable logic: `computeFileSha256()` in `native/qt6/src/db.cpp`, `buildSequencePaths()` in `native/qt6/src/assets_model.cpp`, `isVideoExtension()` in `native/qt6/src/live_preview_manager.cpp`.
- Large UI/controller classes split setup and behavior into focused methods instead of one constructor body: `setupUi()`, `setupToolbar()`, `setupViews()`, `setupPreviewPanel()` in `native/qt6/src/file_manager_pane.h`.

**Parameters:**
- Pass Qt value types by `const &` for strings and containers: `const QString& filePath` in `native/qt6/src/importer.cpp`, `const QModelIndex& idx` in `native/qt6/src/assets_model.h`.
- Default `QObject* parent = nullptr` for QObject-derived classes: `FileManagerPane(QWidget *parent = nullptr)` in `native/qt6/src/file_manager_pane.h`, `LivePreviewManager(QObject* parent = nullptr)` in `native/qt6/src/live_preview_manager.h`.

**Return Values:**
- Use direct Qt/C++ types rather than wrappers: `QVariant`, `QVariantMap`, `QStringList`, `bool`, `int` in `native/qt6/src/assets_model.h`.
- For cache/query lookups, return empty objects for misses: `LivePreviewManager::FrameHandle{}` in `native/qt6/src/live_preview_manager.cpp`, empty `QString()` in `native/qt6/src/db.cpp`.

## Module Design

**Exports:**
- Most headers export one primary class or one cohesive set of related types: `native/qt6/src/log_manager.h`, `native/qt6/src/live_preview_manager.h`, `native/qt6/src/assets_model.h`.
- Qt-facing modules expose signals/slots and `Q_PROPERTY` directly in headers: `native/qt6/src/assets_model.h`, `native/qt6/src/file_manager_pane.h`, `native/qt6/src/log_manager.h`.
- Singleton access is an established pattern for shared services: `DB::instance()` in `native/qt6/src/db.cpp`, `LogManager::instance()` in `native/qt6/src/log_manager.h`, `LivePreviewManager::instance()` in `native/qt6/src/live_preview_manager.cpp`.

**Barrel Files:**
- Not used. Include concrete headers directly from `native/qt6/src/`.

## Additional Observed Prescriptions

- Use Qt signal/slot wiring with `connect(...)` rather than manual callback plumbing, as in `native/qt6/src/grid_scrub.cpp` and `native/qt6/src/mainwindow.cpp`.
- Guard optional features with both definition and value checks, matching `.github/copilot-instructions.md` and `native/qt6/src/live_preview_manager.cpp`: `#if defined(HAVE_TLRENDER) && HAVE_TLRENDER`.
- Protect shared threaded state with Qt lock helpers, not ad hoc synchronization: `QMutexLocker` in `native/qt6/src/db.cpp` and `native/qt6/src/live_preview_manager.cpp`, `QReadLocker` / `QWriteLocker` in `native/qt6/src/live_preview_manager.cpp`.
- Preserve existing mixed header-guard style. Some headers use `#pragma once` such as `native/qt6/src/assets_model.h`; others use classic guards such as `native/qt6/src/file_manager_pane.h`. Match the surrounding directory/file style instead of rewriting globally.

---

*Convention analysis: 2026-04-11*
