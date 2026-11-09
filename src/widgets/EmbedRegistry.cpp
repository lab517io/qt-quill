#include "quill/EmbedRegistry.h"

#include "ImageLoader.h"
#include "quill/Embeds.h"
#include "quill/Formats.h"

#include <QAbstractTextDocumentLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextObjectInterface>

namespace quill {

// --- EmbedRenderer ------------------------------------------------------------------

EmbedRenderer::~EmbedRenderer() = default;
bool EmbedRenderer::accepts(const QJsonValue&) const
{
    return true;
}
bool EmbedRenderer::isBlock() const
{
    return false;
}
bool EmbedRenderer::activatesOnDoubleClick() const
{
    return false;
}
bool EmbedRenderer::activate(QuillEditor*, const EmbedContext&)
{
    return false;
}
QString EmbedRenderer::toolTip(const EmbedContext&) const
{
    return {};
}

// --- CustomEmbedRenderer ------------------------------------------------------------

CustomEmbedRenderer::CustomEmbedRenderer(QString type) : m_type(std::move(type)) {}

QString CustomEmbedRenderer::key() const
{
    return QStringLiteral("custom");
}

bool CustomEmbedRenderer::accepts(const QJsonValue& data) const
{
    const auto parsed = parse(data);
    return parsed && parsed->first == m_type;
}

bool CustomEmbedRenderer::isBlock() const
{
    return true;
}

std::optional<std::pair<QString, QJsonValue>> CustomEmbedRenderer::parse(const QJsonValue& data)
{
    QJsonObject obj;
    if (data.isString()) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(data.toString().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return std::nullopt;
        obj = doc.object();
    } else if (data.isObject()) {
        obj = data.toObject();
    } else {
        return std::nullopt;
    }
    if (obj.size() != 1)
        return std::nullopt;
    return std::make_pair(obj.begin().key(), obj.begin().value());
}

QString CustomEmbedRenderer::encode(const QString& type, const QJsonValue& data)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{type, data}}).toJson(QJsonDocument::Compact));
}

// --- Dispatch from the document layout ----------------------------------------------

namespace {

/// Registered on a document layout for images and embeds; forwards to the
/// registry's handlers.
class EmbedObject : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    explicit EmbedObject(EmbedRegistry* registry) : QObject(registry), m_registry(registry) {}

    EmbedRegistry* registry() const { return m_registry; }

    QSizeF intrinsicSize(QTextDocument* doc, int pos, const QTextFormat& format) override
    {
        const EmbedContext c = m_registry->context(doc, pos, format);
        QSizeF s = m_registry->rendererFor(c.key, c.data)->size(c);
        if (c.availableWidth > 0 && s.width() > c.availableWidth)
            s = QSizeF(c.availableWidth, s.height() * c.availableWidth / s.width());
        return s.expandedTo(QSizeF(1, 1));
    }

    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument* doc, int pos,
                    const QTextFormat& format) override
    {
        const EmbedContext c = m_registry->context(doc, pos, format);
        painter->save();
        m_registry->rendererFor(c.key, c.data)->paint(painter, rect, c);
        painter->restore();
    }

private:
    EmbedRegistry* m_registry;
};

bool isRemote(const QUrl& url)
{
    return url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https");
}

QImage toImage(const QVariant& v)
{
    if (v.canConvert<QImage>() && v.metaType().id() == QMetaType::QImage)
        return v.value<QImage>();
    if (v.metaType().id() == QMetaType::QPixmap)
        return v.value<QPixmap>().toImage();
    if (v.metaType().id() == QMetaType::QByteArray) {
        QImage image;
        image.loadFromData(v.toByteArray());
        return image;
    }
    return {};
}

} // namespace

class EmbedRegistry::Private
{
public:
    explicit Private(EmbedRegistry* q) : q(q), object(new EmbedObject(q)) {}

    detail::ImageLoader* loader()
    {
        if (!m_loader) {
            m_loader = new detail::ImageLoader(q);
            m_loader->setCacheLimit(cacheLimit);
            if (manager)
                m_loader->setManager(manager);
            QObject::connect(m_loader, &detail::ImageLoader::finished, q,
                             [this](const QUrl& url, bool ok) { finished(url, ok); });
        }
        return m_loader;
    }

    void finished(const QUrl& url, bool ok)
    {
        const QList<QPointer<QTextDocument>> docs = waiting.take(url);
        const QImage image = ok ? m_loader->get(url) : QImage();
        for (const QPointer<QTextDocument>& doc : docs) {
            if (!doc)
                continue;
            // Lay out every image of the document that shows this URL again.
            for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
                for (auto it = b.begin(); !it.atEnd(); ++it) {
                    const QTextFragment f = it.fragment();
                    if (!f.charFormat().isImageFormat())
                        continue;
                    const QString name = f.charFormat().toImageFormat().name();
                    if (resolveImageUrl(doc, name) != url)
                        continue;
                    if (ok)
                        doc->addResource(QTextDocument::ImageResource, QUrl(name), image);
                    doc->markContentsDirty(f.position(), f.length());
                }
            }
        }
        Q_EMIT q->imageLoaded(url, ok);
    }

    EmbedRegistry* q;
    EmbedObject* object;
    detail::ImageLoader* m_loader = nullptr;
    QPointer<QNetworkAccessManager> manager;
    qint64 cacheLimit = 64 * 1024 * 1024;
    bool remote = false;
    QHash<QUrl, QList<QPointer<QTextDocument>>> waiting;
};

// --- EmbedRegistry -------------------------------------------------------------------

EmbedRegistry::EmbedRegistry(QObject* parent)
    : QObject(parent), m_fallback(std::make_unique<GenericEmbed>()), d(std::make_unique<Private>(this))
{
    emplace<ImageEmbed>();
    emplace<VideoEmbed>();
    emplace<FormulaEmbed>();
}

EmbedRegistry::~EmbedRegistry() = default;

EmbedRegistry* EmbedRegistry::shared()
{
    static EmbedRegistry* registry = new EmbedRegistry;
    return registry;
}

EmbedRegistry* EmbedRegistry::registryOf(const QTextDocument* doc)
{
    if (!doc)
        return nullptr;
    QTextObjectInterface* iface = doc->documentLayout()->handlerForObject(prop::EmbedObjectType);
    auto* object = dynamic_cast<EmbedObject*>(iface);
    return object ? object->registry() : nullptr;
}

void EmbedRegistry::registerRenderer(std::unique_ptr<EmbedRenderer> handler)
{
    if (handler)
        m_handlers.push_back(std::move(handler));
}

EmbedRenderer* EmbedRegistry::rendererFor(const QString& key, const QJsonValue& data) const
{
    for (auto it = m_handlers.rbegin(); it != m_handlers.rend(); ++it) {
        if ((*it)->key() == key && (*it)->accepts(data))
            return it->get();
    }
    return m_fallback.get();
}

void EmbedRegistry::attachTo(QTextDocument* doc)
{
    if (!doc || registryOf(doc) == this)
        return;
    QAbstractTextDocumentLayout* layout = doc->documentLayout();
    layout->registerHandler(prop::EmbedObjectType, d->object);
    layout->registerHandler(QTextFormat::ImageObject, d->object);
}

EmbedContext EmbedRegistry::context(QTextDocument* doc, int position, const QTextFormat& format)
{
    EmbedContext c;
    c.document = doc;
    c.position = position;
    c.format = format.toCharFormat();
    c.registry = this;
    c.key = format.stringProperty(prop::EmbedType);
    const bool image = format.isImageFormat();
    if (c.key.isEmpty() && image)
        c.key = QStringLiteral("image");
    if (image && c.key == QLatin1String("image"))
        c.data = format.toImageFormat().name();
    else
        c.data = format.property(prop::Embed).toJsonObject().value(c.key);
    c.attributes = format.property(prop::InlineAttributes).toJsonObject();

    const QTextBlock block = doc->findBlock(position);
    c.aloneOnLine = block.isValid() && block.length() == 2;
    qreal width = doc->textWidth();
    if (width <= 0)
        width = doc->pageSize().width();
    if (width > 0 && block.isValid()) {
        const QTextBlockFormat bf = block.blockFormat();
        width -= 2 * doc->documentMargin() + bf.leftMargin() + bf.rightMargin() + std::max(0.0, bf.textIndent());
        int indent = bf.indent();
        if (const QTextList* list = block.textList())
            indent += list->format().indent();
        width -= indent * doc->indentWidth();
        c.availableWidth = std::max(16.0, width);
    }
    return c;
}

void EmbedRegistry::setRemoteImagesEnabled(bool on)
{
    d->remote = on;
}
bool EmbedRegistry::remoteImagesEnabled() const
{
    return d->remote;
}

void EmbedRegistry::setNetworkAccessManager(QNetworkAccessManager* manager)
{
    d->manager = manager;
    if (d->m_loader)
        d->m_loader->setManager(manager);
}

QNetworkAccessManager* EmbedRegistry::networkAccessManager() const
{
    return d->loader()->manager();
}

void EmbedRegistry::setImageCacheLimit(qint64 bytes)
{
    d->cacheLimit = bytes;
    if (d->m_loader)
        d->m_loader->setCacheLimit(bytes);
}

QUrl EmbedRegistry::resolveImageUrl(const QTextDocument* doc, const QString& source)
{
    const QUrl base = doc ? doc->baseUrl() : QUrl();
    // Plain file paths, as flutter_quill stores picked images: C:\x.png or
    // C:/x.png (QUrl would take "C:" for a scheme), UNC \\server\share,
    // and absolute "/x.png" unless the document has a web base URL.
    const bool drive = source.size() > 2 && source.at(0).isLetter() && source.at(1) == QLatin1Char(':')
                       && (source.at(2) == QLatin1Char('/') || source.at(2) == QLatin1Char('\\'));
    const bool unc = source.startsWith(QLatin1String("\\\\"));
    const bool absolute =
        source.startsWith(QLatin1Char('/')) && !source.startsWith(QLatin1String("//")) && !isRemote(base);
    if (drive || unc || absolute)
        return QUrl::fromLocalFile(QString(source).replace(QLatin1Char('\\'), QLatin1Char('/')));
    const QUrl url(source);
    if (url.isRelative() && base.isValid())
        return base.resolved(url);
    return url;
}

QImage EmbedRegistry::image(QTextDocument* doc, const QString& source)
{
    if (!doc || source.isEmpty())
        return {};
    const QUrl name(source);
    const QUrl url = resolveImageUrl(doc, source);
    if (url.isLocalFile() && !source.startsWith(QLatin1String("file:"))) {
        // A plain path: QTextDocument cannot load it by name, so load it here.
        const QVariant added = doc->resource(QTextDocument::ImageResource, name);
        if (added.isValid())
            return toImage(added);
        const QImage image(url.toLocalFile());
        if (!image.isNull())
            doc->addResource(QTextDocument::ImageResource, name, image);
        return image;
    }
    if (!isRemote(url)) {
        const QVariant resource = doc->resource(QTextDocument::ImageResource, name);
        const QImage image = toImage(resource);
        // Files load as bytes; keep the decoded image instead.
        if (!image.isNull() && resource.metaType().id() != QMetaType::QImage)
            doc->addResource(QTextDocument::ImageResource, name, image);
        return image;
    }
    // Resources added for remote URLs (by a host, or after loading) win.
    const QVariant added = doc->resource(QTextDocument::ImageResource, name);
    if (added.isValid())
        return toImage(added);
    if (!d->remote)
        return {};
    const QImage image = d->loader()->get(url);
    if (!image.isNull()) {
        doc->addResource(QTextDocument::ImageResource, name, image);
        return image;
    }
    if (d->loader()->state(url) == detail::ImageLoader::State::Loading) {
        QList<QPointer<QTextDocument>>& docs = d->waiting[url];
        if (!docs.contains(doc))
            docs.append(doc);
    }
    return {};
}

EmbedRegistry::ImageState EmbedRegistry::imageState(QTextDocument* doc, const QString& source) const
{
    if (!doc || source.isEmpty())
        return ImageState::Failed;
    const QUrl url = resolveImageUrl(doc, source);
    if (doc->resource(QTextDocument::ImageResource, QUrl(source)).isValid())
        return ImageState::Loaded;
    if (!isRemote(url))
        return ImageState::Failed;
    if (!d->remote || !d->m_loader)
        return ImageState::Unknown;
    switch (d->m_loader->state(url)) {
    case detail::ImageLoader::State::Loading:
        return ImageState::Loading;
    case detail::ImageLoader::State::Loaded:
        return ImageState::Loaded;
    case detail::ImageLoader::State::Failed:
        return ImageState::Failed;
    case detail::ImageLoader::State::Unknown:
        break;
    }
    return ImageState::Unknown;
}

} // namespace quill

#include "EmbedRegistry.moc"
