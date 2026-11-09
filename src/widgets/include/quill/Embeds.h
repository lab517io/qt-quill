#pragma once

// Built-in embed handlers (milestone M7). EmbedRegistry registers ImageEmbed,
// VideoEmbed and FormulaEmbed; GenericEmbed draws everything else.

#include "quill/EmbedRegistry.h"

#include <QFont>
#include <QImage>

#include <functional>

namespace quill {

/// {"image": url}. width / height attributes and the CSS "style" set the
/// size (flutter_quill_extensions); without them the image keeps its own
/// size, shrunk to the line width. Missing images show a broken-image box,
/// remote images a placeholder while loading.
class ImageEmbed : public EmbedRenderer
{
public:
    QString key() const override;
    bool accepts(const QJsonValue& data) const override;
    bool isBlock() const override;
    QSizeF size(const EmbedContext& c) const override;
    void paint(QPainter* painter, const QRectF& rect, const EmbedContext& c) const override;
    bool activatesOnDoubleClick() const override;
    QString toolTip(const EmbedContext& c) const override;

    /// Size of an image without size attributes that is not loaded (yet).
    static constexpr double PlaceholderWidth = 200, PlaceholderHeight = 150;
};

/// {"video": url}: a 16:9 frame with a play button and the URL; the host
/// plays it on embedActivated(). Alone on its line it takes the line width
/// up to 640 px. width / height attributes override the size.
class VideoEmbed : public EmbedRenderer
{
public:
    /// Thumbnail for a video URL; a null image draws the default frame.
    using ThumbnailProvider = std::function<QImage(const QString& url)>;

    QString key() const override;
    bool isBlock() const override;
    QSizeF size(const EmbedContext& c) const override;
    void paint(QPainter* painter, const QRectF& rect, const EmbedContext& c) const override;
    QString toolTip(const EmbedContext& c) const override;

    void setThumbnailProvider(ThumbnailProvider provider) { m_thumbnail = std::move(provider); }

private:
    ThumbnailProvider m_thumbnail;
};

/// {"formula": tex}: the TeX source in a framed monospace box, inline. A
/// renderer, e.g. one based on MicroTeX or KaTeX, can draw real formulas.
class FormulaEmbed : public EmbedRenderer
{
public:
    /// Renders TeX in the given font and colour; a null image falls back to
    /// the source box. Called on every layout, so cache inside if slow.
    using Renderer = std::function<QImage(const QString& tex, const QFont& font, const QColor& color)>;

    QString key() const override;
    QSizeF size(const EmbedContext& c) const override;
    void paint(QPainter* painter, const QRectF& rect, const EmbedContext& c) const override;
    QString toolTip(const EmbedContext& c) const override;

    void setRenderer(Renderer renderer) { m_renderer = std::move(renderer); }

private:
    QImage rendered(const EmbedContext& c) const;
    Renderer m_renderer;
};

/// Fallback for embeds without a handler: an inline chip with the embed key
/// (or "custom: <type>" for unknown custom embeds). The payload is kept.
class GenericEmbed : public EmbedRenderer
{
public:
    QString key() const override;
    QSizeF size(const EmbedContext& c) const override;
    void paint(QPainter* painter, const QRectF& rect, const EmbedContext& c) const override;
    QString toolTip(const EmbedContext& c) const override;

    static QString label(const EmbedContext& c);
};

} // namespace quill
