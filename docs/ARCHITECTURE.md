## Architecture overview

This document summarizes core runtime subsystems and cross‑cutting concerns: threading/I‑O, logging, and data persistence.

### Threading and I/O model

- UI thread (Qt Widgets)
  - Owns MainWindow and all widgets/models.
  - Must remain responsive; do not perform blocking filesystem or heavy CPU work here.

- Background work
  - LivePreviewManager
    - Decoding and thumbnail generation run off the UI thread via QtConcurrent::run().
    - Results are delivered back to the UI using queued connections.
    - Uses QCache with LRU semantics and a small metadata cache for image sequences.
  - Media conversion
    - Runs outside the UI thread (worker/queue). Pause/Resume is intentionally disabled per product spec.
  - Importer
    - Runs synchronously today because the DB layer uses a single Qt SQL connection that is not thread‑safe across threads.
    - Emits progress signals for UI feedback and batches inserts inside transactions for speed.
    - If we later move importing off the UI thread, we must first introduce per‑thread QSqlDatabase connections (connection‑per‑thread) and adapt DB::instance() accordingly.

Guidelines
- Any new long‑running or blocking work must be scheduled on a worker thread (QtConcurrent/QThreadPool) and communicate back via signals/slots (queued connections).
- Keep QSqlDatabase usage confined to the thread that opened the connection. Do not pass QSqlQuery/QSqlDatabase across threads.
- File operations for the File Manager must use OS handlers (Explorer/Shell) and the existing FileOpsQueue.

### Logging

- Centralized message handling: qInstallMessageHandler(customMessageHandler) in main.cpp installs a single handler that funnels Qt logs to LogManager.
- LogManager
  - Thread‑safe; keeps an in‑memory ring buffer (last 1000 entries) and writes through to app.log next to the executable.
  - Asynchronously appends logs via QMetaObject::invokeMethod(Qt::QueuedConnection) when receiving messages from other threads.
- Do not install additional message handlers elsewhere. Use qDebug/qWarning/qCritical normally; they are captured and persisted by LogManager.
- Avoid logging sensitive data (credentials, tokens). File paths and generic error messages are acceptable.

### Data persistence

- Database and user data are stored under QStandardPaths::AppDataLocation and survive application updates.
- On first run after an update, the app migrates legacy data from the old app directory to AppData.

### Testing and coverage

- Unit tests use QtTest. DB‑backed tests initialize a temporary SQLite DB.
- Code coverage can be enabled on GCC/Clang with -DENABLE_COVERAGE=ON (CI job "coverage-ubuntu" runs gcovr and publishes artifacts).


