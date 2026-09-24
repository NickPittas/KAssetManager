#pragma once

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QThreadPool>
#include <QVariant>

#include <atomic>
#include <functional>

class MediaAsyncRequest : public QObject {
    Q_OBJECT
public:
    using RequestId = quint64;
    using Work = std::function<QVariant()>;

    explicit MediaAsyncRequest(QObject* parent = nullptr);
    ~MediaAsyncRequest() override;

    RequestId submitLatest(const QString& channel, Work work);
    RequestId submit(const QString& channel, Work work);

    void cancel(RequestId requestId);
    void cancelChannel(const QString& channel);
    void cancelAll();

    bool isCurrent(RequestId requestId) const;
    int activeRequestCount() const;

signals:
    void requestFinished(quint64 requestId, const QString& channel, const QVariant& result);
    void requestFailed(quint64 requestId, const QString& channel, const QString& error);

private:
    Q_DISABLE_COPY_MOVE(MediaAsyncRequest)

    RequestId nextRequestId();
    void recordRequest(RequestId requestId, const QString& channel, bool latestOnly);
    bool takeForDelivery(RequestId requestId, const QString& channel);

    mutable QMutex m_mutex;
    QHash<RequestId, QString> m_activeRequests;
    QHash<QString, RequestId> m_latestByChannel;
    std::atomic<RequestId> m_nextRequestId{1};
};
