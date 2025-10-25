#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include <QObject>
#include <QStringList>
#include <QMutex>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
class LogManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList logs READ logs NOTIFY logsChanged)

public:
    static LogManager& instance() {
        static LogManager inst;
        return inst;
    }

    QStringList logs() const { return m_logs; }

    Q_INVOKABLE void addLog(const QString& message, const QString& level = "INFO");
    Q_INVOKABLE void clear();

signals:
    void logsChanged();
    void logAdded(const QString& message);

private:
    explicit LogManager(QObject* parent = nullptr);
    QStringList m_logs;
    QMutex m_mutex;
    QFile m_file;
    QTextStream m_ts;
    static constexpr int MAX_LOGS = 1000;
};

// Custom message handler for qDebug/qWarning/qCritical
void customMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

#endif // LOG_MANAGER_H
