#include "log_manager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QCoreApplication>

LogManager::LogManager(QObject* parent) : QObject(parent) {
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(FLUSH_INTERVAL_MS);
    connect(&m_flushTimer, &QTimer::timeout, this, &LogManager::flushPending);

    // Open persistent app log next to the executable
    QString path = QCoreApplication::applicationDirPath() + "/app.log";
    m_file.setFileName(path);
    if (m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_ts.setDevice(&m_file);
        m_ts << "\n--- session start ---\n";
        m_ts.flush();
    }
    // Install custom message handler to capture qDebug/qWarning/qCritical
    qInstallMessageHandler(customMessageHandler);
}

LogManager::~LogManager() {
    flushPending();
    if (m_ts.device()) {
        m_ts.flush();
    }
}

void LogManager::addLog(const QString& message, const QString& level) {
    QString logEntry;
    {
        QMutexLocker locker(&m_mutex);
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        logEntry = QString("[%1] [%2] %3").arg(timestamp, level, message);
        m_logs.append(logEntry);
        if (m_logs.size() > MAX_LOGS) {
            m_logs.removeFirst();
        }
    } // unlock before emitting signals to avoid UI thread deadlocks

    emit logsChanged();
    emit logAdded(logEntry);

    // Write-through to disk log with buffered flushing
    if (m_ts.device()) {
        m_ts << logEntry << '\n';
        scheduleFlush(level);
    }
}

void LogManager::flushPending() {
    if (!m_ts.device()) {
        m_pendingFlush = false;
        return;
    }
    if (m_pendingFlush) {
        m_ts.flush();
        m_pendingFlush = false;
    }
    if (m_flushTimer.isActive()) {
        m_flushTimer.stop();
    }
}

bool LogManager::shouldFlushImmediately(const QString& level) const {
    const QString upper = level.toUpper();
    return upper == "WARN" || upper == "ERROR" || upper == "FATAL";
}

void LogManager::scheduleFlush(const QString& level) {
    if (!m_ts.device()) {
        return;
    }

    m_pendingFlush = true;

    if (shouldFlushImmediately(level)) {
        m_flushTimer.stop();
        flushPending();
        return;
    }

    m_flushTimer.start(FLUSH_INTERVAL_MS);
}

void LogManager::clear() {
    QMutexLocker locker(&m_mutex);
    m_logs.clear();
    emit logsChanged();
}

void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    QString level;
    switch (type) {
        case QtDebugMsg:
            level = "DEBUG";
            break;
        case QtInfoMsg:
            level = "INFO";
            break;
        case QtWarningMsg:
            level = "WARN";
            break;
        case QtCriticalMsg:
            level = "ERROR";
            break;
        case QtFatalMsg:
            level = "FATAL";
            break;
    }
    
    // Add to log manager asynchronously to avoid re-entrancy/binding loops in QML
    QString levelCopy = level;
    QString msgCopy = msg;
    QMetaObject::invokeMethod(&LogManager::instance(), [levelCopy, msgCopy]() {
        LogManager::instance().addLog(msgCopy, levelCopy);
    }, Qt::QueuedConnection);

    // Also output to stderr for debugging
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    fprintf(stderr, "[%s] [%s] %s\n",
            timestamp.toLocal8Bit().constData(),
            levelCopy.toLocal8Bit().constData(),
            msgCopy.toLocal8Bit().constData());
    fflush(stderr);

    if (type == QtFatalMsg) {
        abort();
    }
}
