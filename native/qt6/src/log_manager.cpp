#include "log_manager.h"
#include <QDebug>
#include <QMutexLocker>

LogManager::LogManager(QObject* parent) : QObject(parent) {
    // Install custom message handler to capture qDebug/qWarning/qCritical
    qInstallMessageHandler(customMessageHandler);
}

void LogManager::addLog(const QString& message, const QString& level) {
    QMutexLocker locker(&m_mutex);
    
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
    QString logEntry = QString("[%1] [%2] %3").arg(timestamp, level, message);
    
    m_logs.append(logEntry);
    
    // Keep only last MAX_LOGS entries
    if (m_logs.size() > MAX_LOGS) {
        m_logs.removeFirst();
    }
    
    emit logsChanged();
    emit logAdded(logEntry);
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
