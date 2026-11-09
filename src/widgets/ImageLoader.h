#pragma once

// Fetches remote images for EmbedRegistry (private header).

#include <QCache>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace quill::detail {

class ImageLoader : public QObject
{
    Q_OBJECT
public:
    enum class State { Unknown, Loading, Loaded, Failed };

    explicit ImageLoader(QObject* parent = nullptr);
    ~ImageLoader() override;

    void setManager(QNetworkAccessManager* manager);
    QNetworkAccessManager* manager();
    /// Cache bound in bytes.
    void setCacheLimit(qint64 bytes);

    /// The cached image for `url`, or a null image. When the image is
    /// neither cached, loading nor failed, a fetch starts.
    QImage get(const QUrl& url);
    State state(const QUrl& url) const;

Q_SIGNALS:
    void finished(const QUrl& url, bool ok);

private:
    void onFinished(QNetworkReply* reply, const QUrl& url);

    QPointer<QNetworkAccessManager> m_external;
    QNetworkAccessManager* m_own = nullptr;
    QCache<QUrl, QImage> m_cache; // cost in KiB
    QSet<QUrl> m_failed;
    QHash<QUrl, QPointer<QNetworkReply>> m_pending;
};

} // namespace quill::detail
