# Testing Patterns

**Analysis Date:** 2026-04-11

## Test Framework

**Runner:**
- QtTest via Qt 6 Test module
- Config: `native/qt6/tests/CMakeLists.txt`

**Assertion Library:**
- QtTest assertions (`QVERIFY`, `QVERIFY2`, `QCOMPARE`, `QSKIP`) in `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_models.cpp`, `native/qt6/tests/test_virtual_drag.cpp`.

**Run Commands:**
```bash
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release   # Configure tests
cmake --build build -j 2                                                                               # Build tests
ctest --test-dir build --output-on-failure                                                             # Run all tests
```

## Test File Organization

**Location:**
- Use a separate test directory at `native/qt6/tests/`.
- Build each test as its own executable in `native/qt6/tests/CMakeLists.txt`.

**Naming:**
- Use `test_<area>.cpp`: `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_importer.cpp`, `native/qt6/tests/test_live_preview_manager.cpp`.
- Keep the CTest name equal to the executable name with `add_test(NAME test_db COMMAND test_db)` in `native/qt6/tests/CMakeLists.txt`.

**Structure:**
```
native/qt6/tests/
├── CMakeLists.txt
├── test_db.cpp
├── test_models.cpp
├── test_importer.cpp
├── test_sequence_detector.cpp
└── test_live_preview_manager.cpp
```

## Test Structure

**Suite Organization:**
```typescript
class TestSequenceDetector : public QObject {
    Q_OBJECT
private slots:
    void testGeneratePattern();
    void testExtractFrameNumber();
    void testIsSequenceFile();
    void testDetectSequences_basic();
};

QTEST_APPLESS_MAIN(TestSequenceDetector)
#include "test_sequence_detector.moc"
```

**Patterns:**
- Define each suite as a `QObject` subclass with test methods under `private slots:` in `native/qt6/tests/test_sequence_detector.cpp`, `native/qt6/tests/test_platform_session.cpp`, and `native/qt6/tests/test_models.cpp`.
- Use `initTestCase()` / `cleanupTestCase()` only when shared setup is valuable, as in `native/qt6/tests/test_db.cpp` and `native/qt6/tests/test_models.cpp`.
- Prefer direct state assertions after real operations, for example `QCOMPARE(model.rowCount({}), 2)` in `native/qt6/tests/test_models.cpp` and `QCOMPARE(seqs.size(), 1)` in `native/qt6/tests/test_sequence_detector.cpp`.

## Mocking

**Framework:**
- No mocking framework is detected.

**Patterns:**
```typescript
target_compile_definitions(test_live_preview_manager PRIVATE
    HAVE_OPENIMAGEIO=0
    HAVE_FFMPEG=0
    HAVE_TLRENDER=0
)
```

- The codebase favors real collaborators with constrained environments over mocks.
- Use temporary files/directories and real Qt objects instead of fake services: `QTemporaryDir` in `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_importer.cpp`, and `native/qt6/tests/test_live_preview_manager.cpp`.
- Disable heavyweight optional integrations at compile time when isolating a unit, as done for `test_live_preview_manager` in `native/qt6/tests/CMakeLists.txt`.

**What to Mock:**
- Not a standard pattern in the current codebase.
- Prefer compile-time feature disabling and temporary filesystem/database setup before introducing mocks.

**What NOT to Mock:**
- Do not replace Qt signals/slots or QtTest wait behavior; use `QSignalSpy` on real objects as in `native/qt6/tests/test_live_preview_manager.cpp` and `native/qt6/tests/test_media_converter_worker.cpp`.
- Do not replace SQLite/file-system behavior for DB/import tests; use real temp-backed inputs as in `native/qt6/tests/test_db.cpp` and `native/qt6/tests/test_importer.cpp`.

## Fixtures and Factories

**Test Data:**
```typescript
QTemporaryDir tmp;
QVERIFY(tmp.isValid());
const QString imgPath = tmp.path() + "/color.png";

QImage img(64, 64, QImage::Format_ARGB32);
img.fill(QColor(200, 10, 10, 255));
QVERIFY(img.save(imgPath));
```

- Build fixtures inline inside the test file.
- Common fixture style is temp directories plus small helper functions such as `writeDummy()` in `native/qt6/tests/test_models.cpp` and `touch()` in `native/qt6/tests/test_importer.cpp`.

**Location:**
- Fixtures live inside each test source file in `native/qt6/tests/`; no shared fixture/factory directory is detected.

## Coverage

**Requirements:** None enforced.

**View Coverage:**
```bash
cmake -S native/qt6 -B build -G Ninja -DBUILD_APP=OFF -DBUILD_TESTS=ON -DENABLE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
gcovr -r native/qt6 --filter native/qt6/src --xml -o build/coverage.xml --txt --branches
```

- Coverage is generated in CI by `.github/workflows/ci.yml` and uploaded as `build/coverage.xml`.

## Test Types

**Unit Tests:**
- Pure logic and platform helpers use app-less QtTest entry points: `native/qt6/tests/test_utils.cpp`, `native/qt6/tests/test_sequence_detector.cpp`, `native/qt6/tests/test_platform_session.cpp`.
- These suites commonly use `QTEST_APPLESS_MAIN(...)` and avoid widget startup.

**Integration Tests:**
- DB/model/import tests exercise real SQLite, filesystem, and model code together: `native/qt6/tests/test_db.cpp`, `native/qt6/tests/test_models.cpp`, `native/qt6/tests/test_importer.cpp`.
- Async/service tests run real objects and wait for emitted signals: `native/qt6/tests/test_live_preview_manager.cpp`, `native/qt6/tests/test_media_converter_worker.cpp`.

**E2E Tests:**
- Not used.

## Common Patterns

**Async Testing:**
```typescript
QSignalSpy spyReady(&mgr, &LivePreviewManager::frameReady);
mgr.requestFrame(imgPath, QSize(32,32), 0.0);
QVERIFY2(spyReady.wait(2000), "frameReady not emitted in time");
```

- Use `QSignalSpy` and bounded waits for asynchronous behavior in `native/qt6/tests/test_live_preview_manager.cpp` and `native/qt6/tests/test_media_converter_worker.cpp`.
- For synchronous-fast paths that may still queue work, check `count()` first and then `wait(...)`, as in `native/qt6/tests/test_media_converter_worker.cpp`.

**Error Testing:**
```typescript
#ifdef Q_OS_WIN
    QSKIP("Linux/non-Windows stub behavior test");
#else
    QVERIFY(!VirtualDrag::startVirtualDrag(files));
#endif
```

- Negative-path assertions are common: `QVERIFY(!search.initialize())` in `native/qt6/tests/test_everything_search.cpp`, `QVERIFY(!handle0.isValid())` in `native/qt6/tests/test_live_preview_manager.cpp`, and the platform-gated stub checks in `native/qt6/tests/test_virtual_drag.cpp`.

## Build and Execution Notes

- Test executables are installed to `build/install_run/bin` and several tests set their working directory there in `native/qt6/tests/CMakeLists.txt`.
- `enable_testing()` lives in `native/qt6/CMakeLists.txt`, and tests are included behind `BUILD_TESTS`.
- CI runs three main modes from `.github/workflows/ci.yml`:
  - release test build on Windows
  - debug build with `ENABLE_ASAN`, `ENABLE_UBSAN`, and `ENABLE_CLANG_TIDY` on Ubuntu
  - coverage build with `ENABLE_COVERAGE` and `gcovr` on Ubuntu

---

*Testing analysis: 2026-04-11*
