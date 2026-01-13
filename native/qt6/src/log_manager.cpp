#include "log_manager.h"
#include <QDebug>
#include <QMutexLocker>
#include <QCoreApplication>

namespace {
bool normalizeErrorLevel(const QString& level, QString* normalizedOut) {
    QString normalized = level.trimmed().toUpper();
    if (normalized == "WARN" || normalized == "WARNING" || normalized == "CRITICAL") {
        normalized = "ERROR";
    }
    if (normalized == "ERROR" || normalized == "FATAL") {
        if (normalizedOut) {
            *normalizedOut = normalized;
        }
        return true;
    }
    return false;
}

bool shouldLogQtMessage(QtMsgType type, QString* levelOut) {
    switch (type) {
        case QtWarningMsg:
        case QtCriticalMsg:
            if (levelOut) {
                *levelOut = "ERROR";
            }
            return true;
        case QtFatalMsg:
            if (levelOut) {
                *levelOut = "FATAL";
            }
            return true;
        case QtDebugMsg:
        case QtInfoMsg:
        default:
            return false;
    }
}
} // namespace

LogManager::LogManager(QObject* parent) : QObject(parent) {
    // Open persistent app log next to the executable
    QString path = QCoreApplication::applicationDirPath() + "/app.log";
    m_file.setFileName(path);
    if (m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_ts.setDevice(&m_file);
    }
}

void LogManager::addLog(const QString& message, const QString& level) {
    QString normalizedLevel;
    if (!normalizeErrorLevel(level, &normalizedLevel)) {
        return;
    }

    QString logEntry;
    {
        QMutexLocker locker(&m_mutex);
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        logEntry = QString("[%1] [%2] %3").arg(timestamp, normalizedLevel, message);
        m_logs.append(logEntry);
        if (m_logs.size() > MAX_LOGS) {
            m_logs.removeFirst();
        }
    } // unlock before emitting signals to avoid UI thread deadlocks

    emit logsChanged();
    emit logAdded(logEntry);

    // Write-through to disk log
    if (m_ts.device()) {
        m_ts << logEntry << '\n';
        m_ts.flush();
    }
}

void LogManager::clear() {
    QMutexLocker locker(&m_mutex);
    m_logs.clear();
    emit logsChanged();
}

void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    Q_UNUSED(context);
    QString level;
    if (!shouldLogQtMessage(type, &level)) {
        return;
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
