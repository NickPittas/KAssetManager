#ifndef PROGRESS_MANAGER_H
#define PROGRESS_MANAGER_H

#include <QObject>
#include <QString>
#include <QMutex>

class ProgressManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isActive READ isActive NOTIFY isActiveChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(int total READ total NOTIFY totalChanged)
    Q_PROPERTY(int percentage READ percentage NOTIFY percentageChanged)

public:
    static ProgressManager& instance() {
        static ProgressManager inst;
        return inst;
    }

    bool isActive() const { return m_isActive; }
    QString message() const { return m_message; }
    int current() const { return m_current; }
    int total() const { return m_total; }
    int percentage() const { 
        return m_total > 0 ? (m_current * 100 / m_total) : 0; 
    }

    Q_INVOKABLE void start(const QString& message, int total = 0);
    Q_INVOKABLE void update(int current, const QString& message = QString());
    Q_INVOKABLE void finish();

signals:
    void isActiveChanged();
    void messageChanged();
    void currentChanged();
    void totalChanged();
    void percentageChanged();

private:
    explicit ProgressManager(QObject* parent = nullptr);
    
    bool m_isActive = false;
    QString m_message;
    int m_current = 0;
    int m_total = 0;
    QMutex m_mutex;
};

#endif // PROGRESS_MANAGER_H
