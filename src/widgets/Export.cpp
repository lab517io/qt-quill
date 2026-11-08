// QTextDocument -> Delta (milestone M4).
//
// Attributes are derived by comparison: start from the attributes stored on
// a format by the loader (or by the editor), recompute the format they would
// produce, and change an attribute only where the actual format differs.
// Styling added by the mapping itself (header size, code background, link
// colour) therefore never turns into attributes, and unknown attributes and
// original value spellings survive untouched.

#include "quill/DeltaDocument.h"

#include "ImageSize.h"

#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextImageFormat>
#include <QTextList>

#include <cmath>

namespace quill {
namespace {

QString key(const char* k) { return QString::fromLatin1(k); }

bool nearlyEqual(double a, double b) { return std::abs(a - b) < 0.01; }

std::optional<double> pointSize(const QTextCharFormat& f)
{
    if (!f.hasProperty(QTextFormat::FontPointSize))
        return std::nullopt;
    return f.fontPointSize();
}

std::optional<QColor> brushColor(const QTextFormat& f, int property)
{
    if (!f.hasProperty(property))
        return std::nullopt;
    const QBrush b = f.brushProperty(property);
    if (b.style() == Qt::NoBrush)
        return std::nullopt;
    return b.color();
}

QStringList families(const QTextCharFormat& f)
{
    QStringList list = f.fontFamilies().toStringList();
    if (list.isEmpty() && f.hasProperty(QTextFormat::FontFamily))
        list << f.stringProperty(QTextFormat::FontFamily);
    return list;
}

bool isBold(const QTextCharFormat& f) { return f.fontWeight() >= QFont::DemiBold; }

void setFlag(AttributeMap& out, const char* name, bool actual, bool expected)
{
    if (actual == expected)
        return;
    if (actual)
        out.insert(key(name), true);
    else
        out.remove(key(name));
}

QString sizeValue(double pointSize, const StyleOptions& style)
{
    const double size = pointSize / style.basePointSize() * style.referenceSize;
    if (nearlyEqual(size, style.sizeSmall))
        return key("small");
    if (nearlyEqual(size, style.sizeLarge))
        return key("large");
    if (nearlyEqual(size, style.sizeHuge))
        return key("huge");
    return detail::formatLength(size);
}

QString fontValue(const QStringList& fams, const QTextCharFormat& f, const StyleOptions& style)
{
    if (fams.isEmpty())
        return {};
    if (f.fontStyleHint() == QFont::Monospace && fams.first() == style.monospaceFamilies.value(0))
        return key("monospace");
    return fams.first();
}

QString alignmentValue(const QTextBlockFormat& f)
{
    if (!f.hasProperty(QTextFormat::BlockAlignment))
        return {};
    const Qt::Alignment a = f.alignment();
    if (a & Qt::AlignJustify)
        return key("justify");
    if (a & Qt::AlignHCenter)
        return key("center");
    if ((a & Qt::AlignRight) && (a & Qt::AlignAbsolute))
        return key("right");
    if ((a & Qt::AlignLeft) && (a & Qt::AlignAbsolute))
        return key("left");
    if (a & Qt::AlignRight) // relative "trailing" right: closest Quill value
        return key("right");
    return {}; // leading / plain left: Quill's default
}

std::optional<double> lineHeightValue(const QTextBlockFormat& f)
{
    if (!f.hasProperty(QTextFormat::LineHeight)
        || f.lineHeightType() != QTextBlockFormat::ProportionalHeight || nearlyEqual(f.lineHeight(), 100.0))
        return std::nullopt;
    return f.lineHeight() / 100.0;
}

QString listValue(const QTextBlock& block)
{
    QTextList* list = block.textList();
    if (!list)
        return {};
    switch (block.blockFormat().marker()) {
    case QTextBlockFormat::MarkerType::Checked:
        return key("checked");
    case QTextBlockFormat::MarkerType::Unchecked:
        return key("unchecked");
    case QTextBlockFormat::MarkerType::NoMarker:
        break;
    }
    switch (list->format().style()) {
    case QTextListFormat::ListDecimal:
    case QTextListFormat::ListLowerAlpha:
    case QTextListFormat::ListUpperAlpha:
    case QTextListFormat::ListLowerRoman:
    case QTextListFormat::ListUpperRoman:
        return key("ordered");
    default:
        return key("bullet");
    }
}

class Exporter
{
public:
    Exporter(const QTextDocument* doc, const StyleOptions& style, QStringList* warnings)
        : m_doc(doc), m_style(style), m_warnings(warnings)
    {
    }

    Delta run(int from, int to)
    {
        Delta delta;
        const int end = m_doc->characterCount();
        from = std::clamp(from, 0, end);
        to = std::clamp(to, from, end);
        if (from == to)
            return delta;
        for (QTextBlock block = m_doc->findBlock(from); block.isValid(); block = block.next()) {
            if (block.position() >= to)
                break;
            checkFrame(block);
            const AttributeMap line = DeltaDocument::lineAttributes(block, m_style);
            // Text in a block styled by quill-qt was styled for the line
            // attributes stored on the block, so inline attributes are derived
            // against those, not against line changes made since (e.g. a header
            // removed with Qt calls). Blocks Qt created on its own (HTML import,
            // setPlainText) carry no stored attributes; their text matches the
            // line as it is now.
            const QTextBlockFormat bf = block.blockFormat();
            const AttributeMap styledFor = bf.hasProperty(prop::BlockAttributes)
                                               ? bf.property(prop::BlockAttributes).toJsonObject()
                                               : line;
            for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                const int fStart = std::max(fragment.position(), from);
                const int fEnd = std::min(fragment.position() + fragment.length(), to);
                if (fStart >= fEnd)
                    continue;
                const QString text = fragment.text().mid(fStart - fragment.position(), fEnd - fStart);
                appendFragment(delta, text, fragment.charFormat(), styledFor, line);
            }
            const int separator = block.position() + block.length() - 1;
            if (separator >= from && separator < to)
                delta.insert(QStringLiteral("\n"), line);
        }
        return delta;
    }

private:
    void checkFrame(const QTextBlock& block)
    {
        if (m_warnedFrame || !m_warnings)
            return;
        QTextCursor c(block);
        if (c.currentFrame() != m_doc->rootFrame()) {
            m_warnedFrame = true;
            m_warnings->append(QStringLiteral(
                "tables and frames have no Delta equivalent; their cells were exported as plain lines"));
        }
    }

    void appendFragment(Delta& delta, const QString& text, const QTextCharFormat& format,
                        const AttributeMap& styledFor, const AttributeMap& line)
    {
        // Embeds are one character each; Qt may merge identical ones into one fragment.
        qsizetype runStart = 0;
        const auto flushText = [&](qsizetype until) {
            if (until > runStart)
                appendText(delta, text.mid(runStart, until - runStart), format, styledFor, line);
        };
        for (qsizetype i = 0; i < text.size(); ++i) {
            if (text.at(i) == QChar::ObjectReplacementCharacter) {
                flushText(i);
                appendEmbed(delta, format, styledFor);
                runStart = i + 1;
            }
        }
        flushText(text.size());
    }

    void appendText(Delta& delta, const QString& text, const QTextCharFormat& format,
                    const AttributeMap& styledFor, const AttributeMap& line)
    {
        const AttributeMap attrs = DeltaDocument::inlineAttributes(format, styledFor, m_style);
        // Soft line breaks (Shift+Enter) do not exist in Quill: each becomes a
        // "\n" carrying the line's attributes, i.e. a line of its own.
        const QStringList parts = text.split(QChar::LineSeparator);
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (i > 0)
                delta.insert(QStringLiteral("\n"), line);
            delta.insert(parts.at(i), attrs);
        }
    }

    void appendEmbed(Delta& delta, const QTextCharFormat& format, const AttributeMap& styledFor)
    {
        AttributeMap attrs = DeltaDocument::inlineAttributes(format, styledFor, m_style);
        QJsonObject embed = format.property(prop::Embed).toJsonObject();
        if (format.isImageFormat()) {
            const QTextImageFormat img = format.toImageFormat();
            if (embed.isEmpty() || (embed.contains(key("image")) && embed.value(key("image")).toString() != img.name()))
                embed = QJsonObject{{key("image"), img.name()}};
            updateImageSize(attrs, img);
            delta.insert(embed, attrs);
            return;
        }
        if (!embed.isEmpty()) {
            delta.insert(embed, attrs);
            return;
        }
        // An object replacement character that is not an embed (e.g. inserted
        // by Qt code): keep it as text so positions stay aligned.
        delta.insert(QString(QChar::ObjectReplacementCharacter), attrs);
    }

    void updateImageSize(AttributeMap& attrs, const QTextImageFormat& img)
    {
        const auto [width, height] = detail::imageSize(attrs);
        const auto update = [&](const char* name, bool has, double actual, std::optional<double> expected) {
            if (!has) {
                if (expected) { // size removed
                    attrs.remove(key(name));
                    stripStyle(name);
                }
                return;
            }
            if (expected && nearlyEqual(*expected, actual))
                return;
            attrs.insert(key(name), detail::formatLength(actual));
            stripStyle(name);
        };
        m_attrsForStyle = &attrs;
        update("width", img.hasProperty(QTextFormat::ImageWidth), img.width(), width);
        update("height", img.hasProperty(QTextFormat::ImageHeight), img.height(), height);
        m_attrsForStyle = nullptr;
    }

    // The CSS "style" wins over width/height, so a changed dimension must
    // leave it, or the old value would come back on load.
    void stripStyle(const char* name)
    {
        AttributeMap& attrs = *m_attrsForStyle;
        const QString style = attrs.value(key("style")).toString();
        if (style.isEmpty() || !detail::parseCss(style).contains(key(name)))
            return;
        const QString rest = detail::removeCssProperties(style, {key(name)});
        if (rest.isEmpty())
            attrs.remove(key("style"));
        else
            attrs.insert(key("style"), rest);
    }

    const QTextDocument* m_doc;
    StyleOptions m_style;
    QStringList* m_warnings;
    bool m_warnedFrame = false;
    AttributeMap* m_attrsForStyle = nullptr;
};

} // namespace

AttributeMap DeltaDocument::inlineAttributes(const QTextCharFormat& actual, const AttributeMap& line,
                                             const StyleOptions& style)
{
    const AttributeMap stored = actual.property(prop::InlineAttributes).toJsonObject();
    AttributeMap out = stored;

    // Structural attributes first: a link brings its own styling (underline,
    // colour), so the remaining properties are compared against a format
    // that already includes the derived link.
    const QTextCharFormat initial = formats::charFormat(stored, line, style);
    const QString actualLink = actual.isAnchor() ? actual.anchorHref() : QString();
    const QString initialLink = initial.isAnchor() ? initial.anchorHref() : QString();
    if (actualLink != initialLink) {
        if (actualLink.isEmpty())
            out.remove(key("link"));
        else
            out.insert(key("link"), actualLink);
    }
    const QTextCharFormat expected = out == stored ? initial : formats::charFormat(out, line, style);

    setFlag(out, "bold", isBold(actual), isBold(expected));
    setFlag(out, "italic", actual.fontItalic(), expected.fontItalic());
    setFlag(out, "underline", actual.fontUnderline(), expected.fontUnderline());
    setFlag(out, "strike", actual.fontStrikeOut(), expected.fontStrikeOut());

    if (actual.verticalAlignment() != expected.verticalAlignment()) {
        switch (actual.verticalAlignment()) {
        case QTextCharFormat::AlignSubScript:
            out.insert(key("script"), key("sub"));
            break;
        case QTextCharFormat::AlignSuperScript:
            out.insert(key("script"), key("super"));
            break;
        default:
            out.remove(key("script"));
        }
    }

    const auto color = [&](const char* name, int property) {
        const auto a = brushColor(actual, property);
        const auto e = brushColor(expected, property);
        if (a == e)
            return;
        if (a)
            out.insert(key(name), colors::format(*a, stored.value(key(name)).toString(), style.colorDialect));
        else
            out.remove(key(name));
    };
    color("color", QTextFormat::ForegroundBrush);
    color("background", QTextFormat::BackgroundBrush);

    const QStringList actualFamilies = families(actual);
    if (actualFamilies != families(expected)) {
        const QString font = fontValue(actualFamilies, actual, style);
        if (font.isEmpty())
            out.remove(key("font"));
        else
            out.insert(key("font"), font);
    }

    const auto aSize = pointSize(actual);
    const auto eSize = pointSize(expected);
    if (aSize.has_value() != eSize.has_value() || (aSize && !nearlyEqual(*aSize, *eSize))) {
        if (!aSize) {
            out.remove(key("size"));
            out.remove(key("small"));
        } else {
            out.insert(key("size"), sizeValue(*aSize, style));
        }
    }
    return out;
}

AttributeMap DeltaDocument::lineAttributes(const QTextBlock& block, const StyleOptions& style)
{
    const QTextBlockFormat actual = block.blockFormat();
    const AttributeMap stored = actual.property(prop::BlockAttributes).toJsonObject();
    const QTextBlockFormat expected = formats::blockFormat(stored, style);
    AttributeMap out = stored;

    if (actual.headingLevel() != expected.headingLevel()) {
        if (actual.headingLevel() >= 1 && actual.headingLevel() <= 6)
            out.insert(key("header"), actual.headingLevel());
        else
            out.remove(key("header"));
    }

    const QString storedGroup = formats::listGroup(stored);
    const QString storedList = storedGroup.isEmpty() ? QString() : stored.value(key("list")).toString();
    const QString list = listValue(block);
    if (list != storedList) {
        if (list.isEmpty())
            out.remove(key("list"));
        else
            out.insert(key("list"), list);
    }

    const int storedIndent = formats::indentLevel(stored);
    int indent = 0;
    if (block.textList())
        indent = std::max(0, block.textList()->format().indent() - 1);
    else if (!storedGroup.isEmpty())
        // Removed from its list with QTextList::remove(), which adds the
        // list's indent (level + 1) to the block's own indent to keep the
        // text in place. Quill keeps the indent level when a list is removed.
        indent = std::max(0, actual.indent() - 1);
    else
        indent = actual.indent();
    if (indent != storedIndent) {
        if (indent > 0)
            out.insert(key("indent"), indent);
        else
            out.remove(key("indent"));
    }

    const QString align = alignmentValue(actual);
    if (align != alignmentValue(expected)) {
        if (align.isEmpty())
            out.remove(key("align"));
        else
            out.insert(key("align"), align);
    }

    const bool rtl = actual.layoutDirection() == Qt::RightToLeft;
    if (rtl != (expected.layoutDirection() == Qt::RightToLeft)) {
        if (rtl)
            out.insert(key("direction"), key("rtl"));
        else
            out.remove(key("direction"));
    }

    const auto lh = lineHeightValue(actual);
    const auto elh = lineHeightValue(expected);
    if (lh.has_value() != elh.has_value() || (lh && !nearlyEqual(*lh, *elh))) {
        if (lh)
            out.insert(key("line-height"), *lh);
        else
            out.remove(key("line-height"));
    }
    return out;
}

Delta DeltaDocument::toDelta(const QTextDocument* doc, const ExportOptions& options, QStringList* warnings)
{
    return toDelta(doc, 0, doc->characterCount(), options, warnings);
}

Delta DeltaDocument::toDelta(const QTextDocument* doc, int from, int to, const ExportOptions& options,
                             QStringList* warnings)
{
    const StyleOptions style = options.style ? effectiveStyle(doc, *options.style) : documentStyle(doc);
    return Exporter(doc, style, warnings).run(from, to);
}

} // namespace quill
