#include "progress_manager.h"
#include <QDebug>
#include <QMutexLocker>
#include "log_manager.h"

ProgressManager::ProgressManager(QObject* parent) : QObject(parent) {
}

void ProgressManager::start(const QString& message, int total) {
    QMutexLocker locker(&m_mutex);
    
    m_isActive = true;
    m_message = message;
    m_current = 0;
    m_total = total;
    
    qDebug() << "Progress started:" << message << "total:" << total;
    LogManager::instance().addLog(QString("Progress started: %1 (%2)").arg(message).arg(total));
    
    emit isActiveChanged();
    emit messageChanged();
    emit currentChanged();
    emit totalChanged();
    emit percentageChanged();
}

void ProgressManager::update(int current, const QString& message) {
    QMutexLocker locker(&m_mutex);
    
    m_current = current;
    if (!message.isEmpty()) {
        m_message = message;
        emit messageChanged();
    }
    
    emit currentChanged();
    emit percentageChanged();
    LogManager::instance().addLog(QString("Progress update: %1 (%2/%3)").arg(m_message).arg(m_current).arg(m_total));
}

void ProgressManager::finish() {
    QMutexLocker locker(&m_mutex);
    
    qDebug() << "Progress finished:" << m_message;
    LogManager::instance().addLog(QString("Progress finished: %1").arg(m_message));
    
    m_isActive = false;
    m_message.clear();
    m_current = 0;
    m_total = 0;
    
    emit isActiveChanged();
    emit messageChanged();
    emit currentChanged();
    emit totalChanged();
    emit percentageChanged();
}
