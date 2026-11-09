#include "ImageLoader.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace quill::detail {

namespace {
constexpr int kTimeoutMs = 30000;
constexpr qint64 kMaxBytes = 64 * 1024 * 1024; // per image download

qsizetype costOf(const QImage& image)
{
    return qsizetype(image.sizeInBytes() / 1024) + 1;
}
} // namespace

ImageLoader::ImageLoader(QObject* parent) : QObject(parent)
{
    setCacheLimit(64 * 1024 * 1024);
}

ImageLoader::~ImageLoader()
{
    for (const QPointer<QNetworkReply>& reply : std::as_const(m_pending)) {
        if (reply) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
}

void ImageLoader::setManager(QNetworkAccessManager* manager)
{
    m_external = manager;
}

QNetworkAccessManager* ImageLoader::manager()
{
    if (m_external)
        return m_external;
    if (!m_own)
        m_own = new QNetworkAccessManager(this);
    return m_own;
}

void ImageLoader::setCacheLimit(qint64 bytes)
{
    m_cache.setMaxCost(qsizetype(std::max<qint64>(1, bytes / 1024)));
}

ImageLoader::State ImageLoader::state(const QUrl& url) const
{
    if (m_cache.contains(url))
        return State::Loaded;
    if (m_pending.contains(url))
        return State::Loading;
    if (m_failed.contains(url))
        return State::Failed;
    return State::Unknown;
}

QImage ImageLoader::get(const QUrl& url)
{
    if (const QImage* cached = m_cache.object(url))
        return *cached;
    if (m_pending.contains(url) || m_failed.contains(url))
        return {};
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = manager()->get(request);
    m_pending.insert(url, reply);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] { onFinished(reply, url); });
    return {};
}

void ImageLoader::onFinished(QNetworkReply* reply, const QUrl& url)
{
    reply->deleteLater();
    m_pending.remove(url);
    QImage image;
    if (reply->error() == QNetworkReply::NoError)
        image.loadFromData(reply->readAll());
    const bool ok = !image.isNull();
    if (ok) {
        // An image larger than the whole cache is not kept here; the
        // documents waiting for it keep it as a resource.
        m_cache.insert(url, new QImage(image), costOf(image));
    } else {
        m_failed.insert(url);
    }
    Q_EMIT finished(url, ok);
}

} // namespace quill::detail
