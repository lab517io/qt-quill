#include "quill/Embeds.h"

#include "ImageSize.h"
#include "quill/Formats.h"

#include <QFileInfo>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>
#include <QTextImageFormat>

namespace quill {

namespace {

QString str(const char* k)
{
    return QString::fromLatin1(k);
}

QFont fontOf(const EmbedContext& c)
{
    QFont f = c.format.font();
    return c.document ? f.resolve(c.document->defaultFont()) : f;
}

/// Small caption font: the document font at 85 %, whatever the line style.
QFont captionFont(const EmbedContext& c)
{
    QFont f = c.document ? c.document->defaultFont() : QFont();
    if (f.pointSizeF() > 0)
        f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.85));
    return f;
}

QColor withAlpha(QColor c, double alpha)
{
    c.setAlphaF(float(c.alphaF() * alpha));
    return c;
}

QString compact(const QJsonValue& v, int max = 200)
{
    QString s = v.isString()
                    ? v.toString()
                    : QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
    if (s.size() > max)
        s = s.left(max - 1) + QChar(0x2026);
    return s;
}

/// A small picture glyph (frame, mountain, sun), struck through when broken.
void drawPictureGlyph(QPainter* p, const QPointF& center, qreal size, const QColor& color, bool broken)
{
    const QRectF r(center.x() - size / 2, center.y() - size * 0.4, size, size * 0.8);
    p->setPen(QPen(color, std::max(1.0, size / 14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(r, size / 10, size / 10);
    QPainterPath m;
    m.moveTo(r.left() + r.width() * 0.12, r.bottom() - r.height() * 0.15);
    m.lineTo(r.left() + r.width() * 0.4, r.top() + r.height() * 0.45);
    m.lineTo(r.left() + r.width() * 0.6, r.top() + r.height() * 0.65);
    m.lineTo(r.left() + r.width() * 0.72, r.top() + r.height() * 0.55);
    m.lineTo(r.right() - r.width() * 0.12, r.bottom() - r.height() * 0.15);
    p->drawPath(m);
    p->drawEllipse(QPointF(r.left() + r.width() * 0.72, r.top() + r.height() * 0.3), size / 14, size / 14);
    if (broken)
        p->drawLine(QPointF(r.left() - size * 0.1, r.bottom() + size * 0.1),
                    QPointF(r.right() + size * 0.1, r.top() - size * 0.1));
}

void drawPlaceholderBox(QPainter* p, const QRectF& rect, const QColor& fg, bool dashed)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(withAlpha(fg, 0.3), 1, dashed ? Qt::DashLine : Qt::SolidLine));
    p->setBrush(withAlpha(fg, 0.05));
    p->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
}

void drawChip(QPainter* p, const QRectF& rect, const QFont& font, const QString& text, const QColor& fg, bool monospace)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(withAlpha(fg, monospace ? 0.35 : 0.25), 1));
    p->setBrush(withAlpha(fg, monospace ? 0.04 : 0.08));
    const qreal radius = monospace ? 3 : rect.height() / 2;
    p->drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p->setFont(font);
    p->setPen(withAlpha(fg, monospace ? 1.0 : 0.8));
    p->drawText(rect, Qt::AlignCenter | Qt::TextSingleLine, text);
}

std::optional<double> attrLength(const AttributeMap& attrs, bool width)
{
    const auto [w, h] = detail::imageSize(attrs);
    return width ? w : h;
}

} // namespace

// --- ImageEmbed ----------------------------------------------------------------------

QString ImageEmbed::key() const
{
    return str("image");
}
bool ImageEmbed::accepts(const QJsonValue& data) const
{
    return data.isString();
}
bool ImageEmbed::isBlock() const
{
    return true;
}
bool ImageEmbed::activatesOnDoubleClick() const
{
    return true;
}

QSizeF ImageEmbed::size(const EmbedContext& c) const
{
    const QTextImageFormat f = c.format.toImageFormat();
    std::optional<double> w, h;
    if (c.format.isImageFormat()) {
        if (f.hasProperty(QTextFormat::ImageWidth))
            w = f.width();
        if (f.hasProperty(QTextFormat::ImageHeight))
            h = f.height();
    } else {
        w = attrLength(c.attributes, true);
        h = attrLength(c.attributes, false);
    }
    if (w && h)
        return QSizeF(*w, *h);
    const QImage image = c.registry ? c.registry->image(c.document, c.data.toString()) : QImage();
    const QSizeF natural = image.isNull() ? QSizeF(PlaceholderWidth, PlaceholderHeight) : image.deviceIndependentSize();
    if (w)
        return QSizeF(*w, *w * natural.height() / natural.width());
    if (h)
        return QSizeF(*h * natural.width() / natural.height(), *h);
    return natural;
}

void ImageEmbed::paint(QPainter* p, const QRectF& rect, const EmbedContext& c) const
{
    const QString source = c.data.toString();
    const QImage image = c.registry ? c.registry->image(c.document, source) : QImage();
    if (!image.isNull()) {
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(rect, image);
        return;
    }
    const QColor fg = p->pen().color();
    const auto state = c.registry ? c.registry->imageState(c.document, source) : EmbedRegistry::ImageState::Failed;
    const bool broken = state == EmbedRegistry::ImageState::Failed;
    drawPlaceholderBox(p, rect, fg, !broken);
    const qreal glyph = std::min({32.0, rect.width() * 0.5, rect.height() * 0.5});
    if (glyph < 8)
        return;
    const QFont font = captionFont(c);
    const QFontMetricsF fm(font);
    const bool caption = rect.height() >= glyph + 2.5 * fm.height() && rect.width() >= 60;
    const QPointF center = rect.center() - QPointF(0, caption ? fm.height() * 0.6 : 0);
    drawPictureGlyph(p, center, glyph, withAlpha(fg, 0.45), broken);
    if (caption) {
        QString text = QFileInfo(EmbedRegistry::resolveImageUrl(c.document, source).path()).fileName();
        if (text.isEmpty())
            text = source.startsWith(str("data:")) ? str("data:") : source;
        if (state == EmbedRegistry::ImageState::Loading)
            text = QObject::tr("Loading %1").arg(text);
        p->setFont(font);
        p->setPen(withAlpha(fg, 0.6));
        const QRectF line(rect.left() + 6, center.y() + glyph * 0.5 + fm.height() * 0.3, rect.width() - 12,
                          fm.height());
        p->drawText(line, Qt::AlignHCenter | Qt::AlignTop | Qt::TextSingleLine,
                    fm.elidedText(text, Qt::ElideMiddle, line.width()));
    }
}

QString ImageEmbed::toolTip(const EmbedContext& c) const
{
    const QString source = c.data.toString();
    if (source.startsWith(str("data:")))
        return QObject::tr("Embedded image");
    return compact(source);
}

// --- VideoEmbed ----------------------------------------------------------------------

QString VideoEmbed::key() const
{
    return str("video");
}
bool VideoEmbed::isBlock() const
{
    return true;
}

QSizeF VideoEmbed::size(const EmbedContext& c) const
{
    const qreal avail = c.availableWidth > 0 ? c.availableWidth : 640;
    const auto w = attrLength(c.attributes, true);
    const auto h = attrLength(c.attributes, false);
    if (w && h)
        return QSizeF(*w, *h);
    if (w)
        return QSizeF(*w, *w * 9 / 16);
    if (h)
        return QSizeF(*h * 16 / 9, *h);
    const qreal width = std::min(avail, c.aloneOnLine ? 640.0 : 320.0);
    return QSizeF(width, width * 9 / 16);
}

void VideoEmbed::paint(QPainter* p, const QRectF& rect, const EmbedContext& c) const
{
    p->setRenderHint(QPainter::Antialiasing);
    QPainterPath frame;
    frame.addRoundedRect(rect, 6, 6);
    p->fillPath(frame, QColor(0x21, 0x21, 0x21));
    const QString url = c.data.isString() ? c.data.toString() : compact(c.data);
    if (m_thumbnail) {
        const QImage thumb = m_thumbnail(url);
        if (!thumb.isNull()) {
            p->save();
            p->setClipPath(frame);
            const QSizeF scaled = thumb.deviceIndependentSize().scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
            QRectF target(QPointF(), scaled);
            target.moveCenter(rect.center());
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            p->drawImage(target, thumb);
            p->restore();
        }
    }
    const qreal radius = std::clamp(std::min(rect.width(), rect.height()) * 0.12, 6.0, 32.0);
    const QPointF center = rect.center();
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(255, 255, 255, 225));
    p->drawEllipse(center, radius, radius);
    QPainterPath play;
    play.moveTo(center + QPointF(-radius * 0.35, -radius * 0.5));
    play.lineTo(center + QPointF(radius * 0.55, 0));
    play.lineTo(center + QPointF(-radius * 0.35, radius * 0.5));
    play.closeSubpath();
    p->fillPath(play, QColor(0x21, 0x21, 0x21));

    const QFont font = captionFont(c);
    const QFontMetricsF fm(font);
    if (rect.height() >= 2 * radius + 3 * fm.height()) {
        p->setFont(font);
        p->setPen(QColor(255, 255, 255, 190));
        const QRectF line(rect.left() + 10, rect.bottom() - fm.height() - 8, rect.width() - 20, fm.height());
        p->drawText(line, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                    fm.elidedText(url, Qt::ElideMiddle, line.width()));
    }
}

QString VideoEmbed::toolTip(const EmbedContext& c) const
{
    return compact(c.data);
}

// --- FormulaEmbed --------------------------------------------------------------------

namespace {
QFont formulaFont(const EmbedContext& c)
{
    QFont f = fontOf(c);
    f.setFamilies(StyleOptions().monospaceFamilies);
    f.setStyleHint(QFont::TypeWriter);
    return f;
}
constexpr qreal kChipPadding = 5;
} // namespace

QString FormulaEmbed::key() const
{
    return str("formula");
}

QImage FormulaEmbed::rendered(const EmbedContext& c) const
{
    if (!m_renderer)
        return {};
    const QColor color =
        c.format.foreground().style() != Qt::NoBrush ? c.format.foreground().color() : QColor(Qt::black);
    return m_renderer(c.data.toString(), fontOf(c), color);
}

QSizeF FormulaEmbed::size(const EmbedContext& c) const
{
    const QImage image = rendered(c);
    if (!image.isNull())
        return image.deviceIndependentSize();
    const QFontMetricsF fm(formulaFont(c));
    const QString tex = c.data.isString() ? c.data.toString() : compact(c.data);
    return QSizeF(fm.horizontalAdvance(tex) + 2 * kChipPadding, fm.height() + 2);
}

void FormulaEmbed::paint(QPainter* p, const QRectF& rect, const EmbedContext& c) const
{
    const QImage image = rendered(c);
    if (!image.isNull()) {
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawImage(rect, image);
        return;
    }
    const QString tex = c.data.isString() ? c.data.toString() : compact(c.data);
    drawChip(p, rect, formulaFont(c), tex, p->pen().color(), true);
}

QString FormulaEmbed::toolTip(const EmbedContext& c) const
{
    return compact(c.data);
}

// --- GenericEmbed --------------------------------------------------------------------

QString GenericEmbed::key() const
{
    return QString();
}

QString GenericEmbed::label(const EmbedContext& c)
{
    if (c.key == QLatin1String("custom")) {
        if (const auto parsed = CustomEmbedRenderer::parse(c.data))
            return parsed->first;
    }
    return c.key.isEmpty() ? QObject::tr("embed") : c.key;
}

QSizeF GenericEmbed::size(const EmbedContext& c) const
{
    const QFontMetricsF fm(fontOf(c));
    return QSizeF(fm.horizontalAdvance(label(c)) + 2 * fm.height() * 0.5, fm.height() + 2);
}

void GenericEmbed::paint(QPainter* p, const QRectF& rect, const EmbedContext& c) const
{
    drawChip(p, rect, fontOf(c), label(c), p->pen().color(), false);
}

QString GenericEmbed::toolTip(const EmbedContext& c) const
{
    return QStringLiteral("%1: %2").arg(c.key, compact(c.data));
}

} // namespace quill
