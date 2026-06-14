#include "media_async_request.h"

#include <QFuture>
#include <QtConcurrent>

#include <exception>

namespace {
struct MediaAsyncResult {
    QVariant value;
    QString error;
};
}

MediaAsyncRequest::MediaAsyncRequest(QObject* parent)
    : QObject(parent)
{
}

MediaAsyncRequest::~MediaAsyncRequest()
{
    cancelAll();
}

MediaAsyncRequest::RequestId MediaAsyncRequest::submitLatest(const QString& channel, Work work)
{
    if (!work)
        return 0;

    const RequestId requestId = nextRequestId();
    recordRequest(requestId, channel, true);

    QtConcurrent::run([work = std::move(work)]() -> MediaAsyncResult {
        try {
            return { work(), {} };
        } catch (const std::exception& e) {
            return { {}, QString::fromUtf8(e.what()) };
        } catch (...) {
            return { {}, QStringLiteral("Media async request failed with an unknown exception") };
        }
    }).then(this, [this, requestId, channel](const MediaAsyncResult& result) {
        if (!takeForDelivery(requestId, channel))
            return;
        if (result.error.isEmpty()) {
            emit requestFinished(requestId, channel, result.value);
        } else {
            emit requestFailed(requestId, channel, result.error);
        }
    });

    return requestId;
}

MediaAsyncRequest::RequestId MediaAsyncRequest::submit(const QString& channel, Work work)
{
    if (!work)
        return 0;

    const RequestId requestId = nextRequestId();
    recordRequest(requestId, channel, false);

    QtConcurrent::run([work = std::move(work)]() -> MediaAsyncResult {
        try {
            return { work(), {} };
        } catch (const std::exception& e) {
            return { {}, QString::fromUtf8(e.what()) };
        } catch (...) {
            return { {}, QStringLiteral("Media async request failed with an unknown exception") };
        }
    }).then(this, [this, requestId, channel](const MediaAsyncResult& result) {
        if (!takeForDelivery(requestId, channel))
            return;
        if (result.error.isEmpty()) {
            emit requestFinished(requestId, channel, result.value);
        } else {
            emit requestFailed(requestId, channel, result.error);
        }
    });

    return requestId;
}

void MediaAsyncRequest::cancel(RequestId requestId)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_activeRequests.find(requestId);
    if (it == m_activeRequests.end())
        return;

    const QString channel = it.value();
    m_activeRequests.erase(it);
    if (m_latestByChannel.value(channel) == requestId)
        m_latestByChannel.remove(channel);
}

void MediaAsyncRequest::cancelChannel(const QString& channel)
{
    QMutexLocker locker(&m_mutex);
    for (auto it = m_activeRequests.begin(); it != m_activeRequests.end(); ) {
        if (it.value() == channel) {
            it = m_activeRequests.erase(it);
        } else {
            ++it;
        }
    }
    m_latestByChannel.remove(channel);
}

void MediaAsyncRequest::cancelAll()
{
    QMutexLocker locker(&m_mutex);
    m_activeRequests.clear();
    m_latestByChannel.clear();
}

bool MediaAsyncRequest::isCurrent(RequestId requestId) const
{
    QMutexLocker locker(&m_mutex);
    return m_activeRequests.contains(requestId);
}

int MediaAsyncRequest::activeRequestCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_activeRequests.size();
}

MediaAsyncRequest::RequestId MediaAsyncRequest::nextRequestId()
{
    return m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
}

void MediaAsyncRequest::recordRequest(RequestId requestId, const QString& channel, bool latestOnly)
{
    QMutexLocker locker(&m_mutex);
    if (latestOnly) {
        const RequestId previous = m_latestByChannel.value(channel, 0);
        if (previous != 0)
            m_activeRequests.remove(previous);
        m_latestByChannel[channel] = requestId;
    }
    m_activeRequests.insert(requestId, channel);
}

bool MediaAsyncRequest::takeForDelivery(RequestId requestId, const QString& channel)
{
    QMutexLocker locker(&m_mutex);
    const auto it = m_activeRequests.find(requestId);
    if (it == m_activeRequests.end() || it.value() != channel)
        return false;

    m_activeRequests.erase(it);
    if (m_latestByChannel.value(channel) == requestId)
        m_latestByChannel.remove(channel);
    return true;
}
