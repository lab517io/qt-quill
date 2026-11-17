#pragma once

// Rendering and interaction for embeds (milestone M7).
//
// An embed is one U+FFFC character in the QTextDocument whose char format
// stores the embed object of the Delta ({"video": "https://..."}) and the
// op attributes. Images use QTextImageFormat; every other embed uses the
// object type prop::EmbedObjectType. An EmbedRegistry attached to a document
// draws both through EmbedRenderers chosen by the embed key and payload, so a
// host application can render its own embeds, or replace the built-ins.
//
// Rendering never changes the document: whatever a handler draws, the embed
// object and its attributes export exactly as they were loaded.

#include "quill/Attributes.h"

#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSizeF>
#include <QString>
#include <QTextCharFormat>
#include <QUrl>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

class QImage;
class QNetworkAccessManager;
class QPainter;
class QRectF;
class QTextDocument;

namespace quill {

class EmbedRegistry;
class QuillEditor;

/// One embed as a handler sees it.
struct EmbedContext
{
    QTextDocument* document = nullptr;
    /// Document position (== Delta index) of the embed character.
    int position = -1;
    /// The embed key: "image", "video", "formula", "custom", "mention", ...
    QString key;
    /// The embed value: {"video": "https://v"} has data "https://v".
    QJsonValue data;
    /// The op attributes (width, height, style, link, bold, ...).
    AttributeMap attributes;
    /// The character's format (font, colours; a QTextImageFormat for images).
    QTextCharFormat format;
    EmbedRegistry* registry = nullptr;
    /// Width available to the embed on its line, in pixels.
    qreal availableWidth = 0;
    /// The embed is the only content of its line (a "block" embed).
    bool aloneOnLine = false;
};

/// Draws one kind of embed. Handlers are owned by an EmbedRegistry and are
/// called from the document layout, so they must not modify the document.
class EmbedRenderer
{
public:
    virtual ~EmbedRenderer();

    /// The embed key handled, e.g. "video".
    virtual QString key() const = 0;
    /// Whether this handler renders `data` (e.g. a "custom" sub-type).
    virtual bool accepts(const QJsonValue& data) const;
    /// flutter_quill BlockEmbed: QuillEditor::insertBlockEmbed and the
    /// toolbar put it on a line of its own.
    virtual bool isBlock() const;

    /// Size of the embed. Kept within c.availableWidth by the caller.
    virtual QSizeF size(const EmbedContext& c) const = 0;
    /// Draws the embed into `rect`, which has the size returned by size().
    /// The painter's pen is the text colour.
    virtual void paint(QPainter* painter, const QRectF& rect, const EmbedContext& c) const = 0;

    /// Whether QuillEditor offers resize handles for this embed (images).
    /// The size is written to the embed's attributes as flutter_quill or
    /// Quill.js do (QuillEditor::setImageSizeAttributes).
    virtual bool resizable() const;

    /// Images activate on a double click, everything else on a click.
    virtual bool activatesOnDoubleClick() const;
    /// Called when the embed is activated in `editor`. Return true when the
    /// handler dealt with it; otherwise QuillEditor emits embedActivated().
    virtual bool activate(QuillEditor* editor, const EmbedContext& c);
    /// Tooltip shown over the embed in QuillEditor.
    virtual QString toolTip(const EmbedContext& c) const;
};

/// Base for handlers of one flutter_quill custom embed type.
///
/// flutter_quill stores a CustomBlockEmbed(type, data) as
/// {"custom": "{\"<type>\": <data>}"}: the value is a JSON *string* of an
/// object with one key. Some versions store the object itself; both are
/// accepted, and the original form is what exports.
class CustomEmbedRenderer : public EmbedRenderer
{
public:
    explicit CustomEmbedRenderer(QString type);

    QString key() const final;
    bool accepts(const QJsonValue& data) const override;
    bool isBlock() const override;
    QString customType() const { return m_type; }

    /// Splits a custom payload into its type and data.
    static std::optional<std::pair<QString, QJsonValue>> parse(const QJsonValue& data);
    /// The custom payload for `type` and `data`, as flutter_quill writes it
    /// (CustomBlockEmbed.toJsonString): a compact JSON string. flutter_quill
    /// can only render custom embeds whose `data` is a string (its
    /// CustomBlockEmbed is a BlockEmbed with String data), so encode
    /// structured data as JSON text first, as its "notes" example does.
    static QString encode(const QString& type, const QJsonValue& data);

private:
    QString m_type;
};

/// The set of handlers used to render the embeds of a document, plus the
/// loader for remote images.
class EmbedRegistry : public QObject
{
    Q_OBJECT
public:
    /// A registry with the built-in handlers (image, video, formula) and the
    /// generic fallback. Remote images are off.
    explicit EmbedRegistry(QObject* parent = nullptr);
    ~EmbedRegistry() override;

    /// The registry DeltaDocument::load() and apply() attach when
    /// LoadOptions::embeds is null. Lives until the program exits.
    static EmbedRegistry* shared();
    /// The registry attached to `doc`, or nullptr.
    static EmbedRegistry* registryOf(const QTextDocument* doc);

    /// Adds a handler. For the same key, later registrations are asked first.
    void registerRenderer(std::unique_ptr<EmbedRenderer> handler);
    /// Constructs and registers a handler; returns it.
    template<class T, class... Args>
    T* emplace(Args&&... args)
    {
        auto h = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = h.get();
        registerRenderer(std::move(h));
        return raw;
    }
    /// The handler drawing an embed: the most recently registered one with
    /// that key whose accepts() is true, else the generic fallback. Never null.
    EmbedRenderer* rendererFor(const QString& key, const QJsonValue& data) const;
    /// The most recently registered handler of type T, or nullptr.
    template<class T>
    T* find() const
    {
        for (auto it = m_handlers.rbegin(); it != m_handlers.rend(); ++it) {
            if (auto* h = dynamic_cast<T*>(it->get()))
                return h;
        }
        return nullptr;
    }
    EmbedRenderer* fallback() const { return m_fallback.get(); }

    /// Makes this registry draw the embeds of `doc` (replacing any other).
    void attachTo(QTextDocument* doc);
    /// What a handler knows about the embed at `position` with `format`.
    EmbedContext context(QTextDocument* doc, int position, const QTextFormat& format);
    /// The size the embed is drawn with: its handler's size, kept within the
    /// available width. The layout may add spacing below it.
    QSizeF drawnSize(QTextDocument* doc, int position, const QTextFormat& format);

    // --- Images ------------------------------------------------------------------
    enum class ImageState { Unknown, Loading, Loaded, Failed };

    /// Fetch http(s) images with networkAccessManager(). Off by default;
    /// QuillEditor's registry turns it on.
    void setRemoteImagesEnabled(bool on);
    bool remoteImagesEnabled() const;
    /// Network manager for remote images, e.g. one that adds auth headers.
    /// Not owned. Default: one owned by the registry.
    void setNetworkAccessManager(QNetworkAccessManager* manager);
    QNetworkAccessManager* networkAccessManager() const;
    /// Bound of the in-memory cache of fetched images, in bytes (default 64 MiB).
    void setImageCacheLimit(qint64 bytes);

    /// The image an image embed with `source` shows in `doc`: a document
    /// resource (data:, file:, qrc:, relative paths), or a fetched remote
    /// image. A remote image that is not loaded yet starts loading and is
    /// null until imageLoaded() is emitted, after which `doc` lays out again.
    QImage image(QTextDocument* doc, const QString& source);
    ImageState imageState(QTextDocument* doc, const QString& source) const;
    /// URL a source string refers to: resolved against doc's base URL.
    static QUrl resolveImageUrl(const QTextDocument* doc, const QString& source);

Q_SIGNALS:
    /// A remote image finished loading (ok) or failed.
    void imageLoaded(const QUrl& url, bool ok);

private:
    class Private;
    friend class Private;
    std::vector<std::unique_ptr<EmbedRenderer>> m_handlers;
    std::unique_ptr<EmbedRenderer> m_fallback;
    std::unique_ptr<Private> d;
};

} // namespace quill
