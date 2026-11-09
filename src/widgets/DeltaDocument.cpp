#include "quill/DeltaDocument.h"

#include "DocumentEdit.h"
#include "ImageSize.h"
#include "quill/EmbedRegistry.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextList>
#include <QUrl>
#include <optional>

#include <vector>

namespace quill {
namespace {

QString compactJson(const QJsonValue& v)
{
    const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}

class Loader
{
public:
    Loader(QTextDocument* doc, const StyleOptions& style) : m_doc(doc), m_style(style), m_cursor(doc) {}

    void run(const Delta& delta, LoadResult& result)
    {
        m_cursor.beginEditBlock();
        qsizetype lineIndex = 0;
        delta.eachLine([&](const Delta& line, const AttributeMap& lineAttrs, qsizetype i) {
            lineIndex = i;
            checkAttributes(lineAttrs, true, i, result);
            const QTextBlockFormat bf = formats::blockFormat(lineAttrs, m_style);
            const QTextCharFormat bcf = detail::blockCharFormat(lineAttrs, m_style);
            if (i == 0) {
                m_cursor.setBlockFormat(bf);
                m_cursor.setBlockCharFormat(bcf);
            } else {
                m_cursor.insertBlock(bf, bcf);
            }
            for (const Op& op : line.ops()) {
                checkAttributes(op.attributes(), false, i, result);
                if (op.isEmbed())
                    insertEmbed(op, lineAttrs, i, result);
                else
                    m_cursor.insertText(op.text(), formats::charFormat(op.attributes(), lineAttrs, m_style));
            }
            return true;
        });
        Q_UNUSED(lineIndex);
        DeltaDocument::regroupLists(m_doc->firstBlock(), m_doc->lastBlock(), m_style);
        DeltaDocument::updateBlockSpacing(m_doc->firstBlock(), m_doc->lastBlock(), m_style);
        m_cursor.endEditBlock();
    }

private:
    void checkAttributes(const AttributeMap& attrs, bool line, qsizetype lineIndex, LoadResult& result)
    {
        for (auto it = attrs.begin(); it != attrs.end(); ++it) {
            const QString& key = it.key();
            if (!formats::inlineKeys().contains(key) && !formats::lineKeys().contains(key)
                && !formats::passiveKeys().contains(key)) {
                result.warnings.append(
                    QStringLiteral("line %1: unknown attribute \"%2\" preserved").arg(lineIndex + 1).arg(key));
            }
        }
        for (const QString& problem : formats::validate(attrs, line, m_style))
            result.warnings.append(QStringLiteral("line %1: %2").arg(lineIndex + 1).arg(problem));
    }

    void insertEmbed(const Op& op, const AttributeMap& lineAttrs, qsizetype lineIndex, LoadResult& result)
    {
        const QJsonObject embed = op.embed();
        const QString type = embed.isEmpty() ? QString() : embed.begin().key();
        if (embed.size() != 1) {
            result.warnings.append(QStringLiteral("line %1: embed should have exactly one key: %2")
                                       .arg(lineIndex + 1)
                                       .arg(compactJson(embed)));
        }
        const QTextCharFormat format = detail::embedCharFormat(embed, op.attributes(), lineAttrs, m_style);
        if (format.isImageFormat()) {
            detail::registerImageResource(m_doc, format.toImageFormat().name());
            m_cursor.insertImage(format.toImageFormat());
        } else {
            // Drawn by the EmbedRegistry attached to the document.
            m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), format);
        }
    }

    QTextDocument* m_doc;
    StyleOptions m_style;
    QTextCursor m_cursor;
};

// QTextList::remove() adds the list's indent to the block to keep the text
// in place; restore the indent the line attributes ask for.
void removeFromList(QTextList* list, const QTextBlock& block, const AttributeMap& line, const StyleOptions& style)
{
    list->remove(block);
    QTextCursor c(block);
    QTextBlockFormat f = block.blockFormat();
    f.setIndent(formats::blockFormat(line, style).indent());
    c.setBlockFormat(f);
}

AttributeMap lineAttributesOf(const QTextBlock& block)
{
    return block.blockFormat().property(prop::BlockAttributes).toJsonObject();
}

} // namespace

StyleOptions DeltaDocument::effectiveStyle(const QTextDocument* doc, const StyleOptions& style)
{
    StyleOptions s = style;
    if (s.baseFont.family().isEmpty() && s.baseFont.families().isEmpty())
        s.baseFont = doc->defaultFont();
    return s;
}

namespace {
constexpr char kStyleProperty[] = "_quillqt_style";
}

StyleOptions DeltaDocument::documentStyle(const QTextDocument* doc)
{
    const QVariant v = doc->property(kStyleProperty);
    if (v.canConvert<StyleOptions>())
        return v.value<StyleOptions>();
    return effectiveStyle(doc, StyleOptions());
}

void DeltaDocument::setDocumentStyle(QTextDocument* doc, const StyleOptions& style)
{
    doc->setProperty(kStyleProperty, QVariant::fromValue(effectiveStyle(doc, style)));
}

LoadResult DeltaDocument::load(QTextDocument* doc, const Delta& deltaIn, const LoadOptions& options)
{
    LoadResult result;
    if (!deltaIn.isDocument()) {
        result.error = QStringLiteral("not a document: only insert operations are allowed");
        return result;
    }
    Delta delta = deltaIn;
    const bool endsWithNewline = !delta.ops().isEmpty() && delta.ops().back().isTextInsert()
                                 && delta.ops().back().text().endsWith(QLatin1Char('\n'));
    if (!endsWithNewline) {
        if (!delta.isEmpty())
            result.warnings.append(QStringLiteral("document does not end with \"\\n\"; one was appended"));
        delta.insert(QStringLiteral("\n"));
    }

    const StyleOptions style = effectiveStyle(doc, options.style);
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    doc->setIndentWidth(style.indentWidth);
    if (!options.baseUrl.isEmpty())
        doc->setBaseUrl(options.baseUrl);

    (options.embeds ? options.embeds : EmbedRegistry::shared())->attachTo(doc);
    Loader(doc, style).run(delta, result);
    setDocumentStyle(doc, style);

    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
    result.ok = true;
    return result;
}

void DeltaDocument::regroupLists(const QTextBlock& firstIn, const QTextBlock& lastIn, const StyleOptions& style)
{
    if (!firstIn.isValid() || !lastIn.isValid())
        return;
    const auto isListLine = [](const QTextBlock& b) { return !formats::listGroup(lineAttributesOf(b)).isEmpty(); };
    // Extend to the whole run of list lines, since numbering depends on it.
    QTextBlock first = firstIn;
    while (first.previous().isValid() && isListLine(first.previous()))
        first = first.previous();
    QTextBlock last = lastIn;
    while (last.next().isValid() && isListLine(last.next()))
        last = last.next();

    struct Level
    {
        QString group;
        QTextList* list = nullptr;
    };
    std::vector<Level> stack;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const AttributeMap line = lineAttributesOf(b);
        const QString group = formats::listGroup(line);
        QTextList* current = b.textList();
        if (group.isEmpty()) {
            stack.clear();
            if (current)
                removeFromList(current, b, line, style);
        } else {
            const int level = formats::indentLevel(line);
            stack.resize(size_t(level) + 1);
            Level& slot = stack[size_t(level)];
            const QTextListFormat wanted = formats::listFormat(line, style);
            if (slot.list && slot.group == group && slot.list->format().style() == wanted.style()
                && slot.list->format().indent() == wanted.indent()) {
                if (current != slot.list)
                    slot.list->add(b);
            } else {
                QTextCursor c(b);
                if (current)
                    removeFromList(current, b, line, style);
                slot.list = c.createList(wanted);
                slot.group = group;
            }
        }
        if (b == last)
            break;
    }
}

namespace {
/// Group of a line for flutter_quill's block spacing: 1 list, 2 quote, 3 code, 0 none.
} // namespace

namespace {

std::optional<AttributeMap> attributesOf(const QTextBlock& b)
{
    if (!b.isValid())
        return std::nullopt;
    return lineAttributesOf(b);
}

formats::LineSpacing spacingOf(const QTextBlock& b, const StyleOptions& style)
{
    const auto prev = attributesOf(b.previous());
    const auto next = attributesOf(b.next());
    return formats::lineSpacing(lineAttributesOf(b), prev ? &*prev : nullptr, next ? &*next : nullptr, style);
}

} // namespace

namespace {

struct Margins
{
    double top = 0, bottom = 0, decoTop = 0, decoBottom = 0;
};

/// The margins updateBlockSpacing gives `b`; `prev` is the spacing of the
/// line above (its bottom spacing goes into this line's top margin).
Margins marginsFor(const QTextBlock& b, const formats::LineSpacing& s, const std::optional<formats::LineSpacing>& prev)
{
    Margins m;
    m.top = s.top + (prev ? prev->bottom : 0);
    m.bottom = b.next().isValid() ? 0 : s.bottom;
    m.decoTop = s.top - s.outerTop;
    m.decoBottom = s.bottom - s.outerBottom;
    return m;
}

bool same(double a, double b)
{
    return qFuzzyCompare(1.0 + a, 1.0 + b);
}

bool hasMargins(const QTextBlockFormat& f, const Margins& m)
{
    return same(f.topMargin(), m.top) && same(f.bottomMargin(), m.bottom)
           && same(f.doubleProperty(prop::DecorationTop), m.decoTop)
           && same(f.doubleProperty(prop::DecorationBottom), m.decoBottom);
}

} // namespace

void DeltaDocument::updateBlockSpacing(const QTextBlock& firstIn, const QTextBlock& lastIn, const StyleOptions& style)
{
    if (!firstIn.isValid() || !lastIn.isValid())
        return;
    // A line's spacing depends on its neighbours, and its top margin holds
    // the previous line's bottom spacing: include one line on each side.
    const QTextBlock first = firstIn.previous().isValid() ? firstIn.previous() : firstIn;
    const QTextBlock last = lastIn.next().isValid() ? lastIn.next() : lastIn;
    std::optional<formats::LineSpacing> prev;
    if (first.previous().isValid())
        prev = spacingOf(first.previous(), style);
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const formats::LineSpacing s = spacingOf(b, style);
        const Margins m = marginsFor(b, s, prev);
        QTextBlockFormat f = b.blockFormat();
        if (!hasMargins(f, m)) {
            f.setTopMargin(m.top);
            f.setBottomMargin(m.bottom);
            const auto setOrClear = [&f](int property, double v) {
                if (v > 0)
                    f.setProperty(property, v);
                else
                    f.clearProperty(property);
            };
            setOrClear(prop::DecorationTop, m.decoTop);
            setOrClear(prop::DecorationBottom, m.decoBottom);
            QTextCursor(b).setBlockFormat(f);
        }
        if (b == last)
            break;
        prev = s;
    }
}

bool detail::blockSpacingStale(const QTextBlock& firstIn, const QTextBlock& lastIn, const StyleOptions& style)
{
    if (!firstIn.isValid() || !lastIn.isValid())
        return false;
    const QTextBlock first = firstIn.previous().isValid() ? firstIn.previous() : firstIn;
    const QTextBlock last = lastIn.next().isValid() ? lastIn.next() : lastIn;
    std::optional<formats::LineSpacing> prev;
    if (first.previous().isValid())
        prev = spacingOf(first.previous(), style);
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const formats::LineSpacing s = spacingOf(b, style);
        if (!hasMargins(b.blockFormat(), marginsFor(b, s, prev)))
            return true;
        if (b == last)
            break;
        prev = s;
    }
    return false;
}

int detail::staleBlockSpacing(const QTextDocument* doc, const StyleOptions& style)
{
    std::optional<formats::LineSpacing> prev;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const formats::LineSpacing s = spacingOf(b, style);
        if (!hasMargins(b.blockFormat(), marginsFor(b, s, prev)))
            return b.blockNumber();
        prev = s;
    }
    return -1;
}

} // namespace quill
