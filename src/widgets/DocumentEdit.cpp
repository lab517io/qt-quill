// Applying change Deltas to a QTextDocument (milestone M5).
//
// A change is applied in two passes. First the text is edited with
// QTextCursor calls (inserts, deletes, block splits), which keeps Qt's own
// cursors, undo stack and layout incremental. Then every line touched by the
// change is restyled from the expected result, `contents.compose(change)`,
// with the same pure format mapping the loader uses, so the document exports
// exactly to that result.

#include "DocumentEdit.h"

#include "ImageSize.h"
#include "quill/DeltaDocument.h"
#include "quill/EmbedRegistry.h"
#include "quill/OpIterator.h"

#include <QByteArray>
#include <QImage>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QUrl>

namespace quill {
namespace detail {

AttributeMap inlineSubset(const AttributeMap& attrs)
{
    AttributeMap out;
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        if (formats::inlineKeys().contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

QTextCharFormat blockCharFormat(const AttributeMap& line, const StyleOptions& style)
{
    return formats::charFormat(inlineSubset(line), line, style);
}

QTextCharFormat embedCharFormat(const QJsonObject& embed, const AttributeMap& attrs, const AttributeMap& line,
                                const StyleOptions& style)
{
    QTextCharFormat base = formats::charFormat(attrs, line, style);
    const QString type = embed.isEmpty() ? QString() : embed.begin().key();
    base.setProperty(prop::EmbedType, type);
    base.setProperty(prop::Embed, embed);

    const QJsonValue data = embed.value(type);
    if (type == QLatin1String("image") && data.isString()) {
        QTextImageFormat img;
        img.merge(base);
        img.setName(data.toString());
        const auto [width, height] = imageSize(attrs);
        if (width)
            img.setWidth(*width);
        if (height)
            img.setHeight(*height);
        return img;
    }
    base.setObjectType(prop::EmbedObjectType);
    // Inline embeds (formulas, chips) centre on the text; images sit on the
    // baseline. Export ignores this alignment (see Export.cpp).
    if (base.verticalAlignment() == QTextCharFormat::AlignNormal)
        base.setVerticalAlignment(QTextCharFormat::AlignMiddle);
    return base;
}

void registerImageResource(QTextDocument* doc, const QString& source)
{
    if (!source.startsWith(QLatin1String("data:")))
        return;
    const QUrl url(source);
    if (doc->resource(QTextDocument::ImageResource, url).isValid())
        return;
    const qsizetype comma = source.indexOf(QLatin1Char(','));
    if (comma < 0)
        return;
    const QString meta = source.mid(5, comma - 5);
    const QByteArray payload = source.mid(comma + 1).toLatin1();
    const QByteArray bytes = meta.endsWith(QLatin1String(";base64")) ? QByteArray::fromBase64(payload)
                                                                     : QByteArray::fromPercentEncoding(payload);
    QImage image;
    if (image.loadFromData(bytes))
        doc->addResource(QTextDocument::ImageResource, url, image);
}

Delta embedInsertChange(const Delta& contents, int index, int length, const QJsonObject& embed, bool block,
                        int* embedIndex)
{
    const Delta removal = Delta().retain(index).remove(length);
    const Delta base = contents.compose(removal);
    Delta insert = Delta().retain(index);
    int at = index;
    const auto textOf = [](const Delta& d) {
        return d.ops().isEmpty() || !d.ops().first().isTextInsert() ? QString() : d.ops().first().text();
    };
    const bool newlineBefore = index == 0 || textOf(base.slice(index - 1, index)) == QLatin1String("\n");
    const bool newlineAfter = textOf(base.slice(index, index + 1)) == QLatin1String("\n");
    if (!block || (newlineBefore && newlineAfter)) {
        insert.insert(embed);
    } else {
        AttributeMap lineStyle;
        const Delta rest = base.slice(index);
        for (const Op& op : rest.ops()) {
            if (op.isTextInsert() && op.text().contains(QLatin1Char('\n'))) {
                lineStyle = op.attributes();
                break;
            }
        }
        if (!newlineBefore) {
            insert.insert(QStringLiteral("\n"), lineStyle);
            ++at;
        }
        insert.insert(embed);
        if (!newlineAfter)
            insert.insert(QStringLiteral("\n"));
    }
    if (embedIndex)
        *embedIndex = at;
    return removal.compose(insert);
}

Delta flutterDeleteChange(const Delta& contents, int index, int length)
{
    const int total = int(contents.length());
    if (length <= 0 || index < 0 || index >= total)
        return {};
    length = std::min(length, total - index);
    const auto charAt = [&](int i) {
        const Delta piece = contents.slice(i, i + 1);
        return piece.ops().isEmpty() ? Op() : piece.ops().first();
    };
    const Op first = charAt(index);
    const bool firstIsNewline = first.isTextInsert() && first.text() == QLatin1String("\n");
    if (index + length >= total) // EnsureLastLineBreakDeleteRule
        return length > 1 ? Delta().retain(index).remove(length - 1) : Delta();
    Delta change = Delta().retain(index).remove(length);
    if (!firstIsNewline)
        return change;
    // PreserveLineStyleOnMergeRule. The line whose "\n" is deleted is empty
    // when it starts at 0 or follows another "\n"; then a next line with
    // text keeps its own format.
    const Op prev = index > 0 ? charAt(index - 1) : Op();
    if (index == 0 || (prev.isTextInsert() && prev.text() == QLatin1String("\n"))) {
        for (int i = index + length; i < total; ++i) {
            const Op op = charAt(i);
            if (op.isTextInsert() && op.text() == QLatin1String("\n"))
                break;
            if (!op.isTextInsert() || !op.text().trimmed().isEmpty())
                return change;
        }
    }
    const AttributeMap upper = first.attributes();
    const Delta rest = contents.slice(index + length);
    int offset = 0;
    for (const Op& op : rest.ops()) {
        const qsizetype nl = op.isTextInsert() ? op.text().indexOf(QLatin1Char('\n')) : -1;
        if (nl < 0) {
            offset += int(op.length());
            continue;
        }
        AttributeMap attrs;
        const AttributeMap lower = op.attributes();
        for (auto it = lower.begin(); it != lower.end(); ++it)
            attrs.insert(it.key(), QJsonValue::Null);
        for (auto it = upper.begin(); it != upper.end(); ++it)
            attrs.insert(it.key(), it.value());
        offset += int(nl);
        change.retain(offset).retain(1, attrs);
        break;
    }
    return change;
}

QString normalizeText(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        switch (ch.unicode()) {
        case '\r':
            out.append(QLatin1Char('\n'));
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            break;
        case QChar::LineSeparator:
        case QChar::ParagraphSeparator:
            out.append(QLatin1Char('\n'));
            break;
        case 0xFDD0: // QTextBeginningOfFrame
        case 0xFDD1: // QTextEndOfFrame
            out.append(QChar(QChar::ReplacementCharacter));
            break;
        default:
            out.append(ch);
        }
    }
    return out;
}

AttributeMap lastAttributes(const Delta& delta)
{
    return delta.ops().isEmpty() ? AttributeMap() : delta.ops().constLast().attributes();
}

namespace {

/// Walks a block's fragments once, in order, for consecutive ranges.
class FragmentWalker
{
public:
    explicit FragmentWalker(const QTextBlock& block) : m_it(block.begin()) {}

    /// Whether every fragment overlapping [from, to) has `format`. Calls
    /// must come with increasing, non-overlapping ranges.
    bool rangeHas(int from, int to, const QTextCharFormat& format)
    {
        while (!m_it.atEnd() && end(m_it.fragment()) <= from)
            ++m_it;
        for (QTextBlock::iterator j = m_it; !j.atEnd(); ++j) {
            const QTextFragment f = j.fragment();
            if (f.position() >= to)
                break;
            if (!(f.charFormat() == format))
                return false;
        }
        return true;
    }

private:
    static int end(const QTextFragment& f) { return f.position() + f.length(); }
    QTextBlock::iterator m_it;
};

} // namespace

void restyleBlock(const QTextBlock& block, const Delta& line, const StyleOptions& style)
{
    const AttributeMap lineAttrs = lastAttributes(line);
    QTextCursor c(block);

    QTextBlockFormat bf = formats::blockFormat(lineAttrs, style);
    const QTextBlockFormat old = block.blockFormat();
    if (old.hasProperty(QTextFormat::ObjectIndex))
        bf.setObjectIndex(old.objectIndex()); // list membership: regroupLists decides
    // Vertical spacing depends on the neighbouring lines: updateBlockSpacing decides.
    bf.setTopMargin(old.topMargin());
    bf.setBottomMargin(old.bottomMargin());
    for (const int property : {prop::DecorationTop, prop::DecorationBottom}) {
        if (old.hasProperty(property))
            bf.setProperty(property, old.property(property));
    }
    if (!(bf == old))
        c.setBlockFormat(bf);
    const QTextCharFormat bcf = blockCharFormat(lineAttrs, style);
    if (!(block.charFormat() == bcf))
        c.setBlockCharFormat(bcf);

    // Find the ranges whose format differs in one pass over the fragments
    // (checking each op from the block start was quadratic in long lines),
    // then change them.
    struct Change
    {
        int from, to;
        QTextCharFormat format;
    };
    QList<Change> changes;
    FragmentWalker fragments(block);
    int pos = block.position();
    const Delta content = line.slice(0, line.length() - 1);
    for (const Op& op : content.ops()) {
        const int n = int(op.length());
        const QTextCharFormat format = op.isEmbed() ? embedCharFormat(op.embed(), op.attributes(), lineAttrs, style)
                                                    : formats::charFormat(op.attributes(), lineAttrs, style);
        if (!fragments.rangeHas(pos, pos + n, format))
            changes.append({pos, pos + n, format});
        pos += n;
    }
    for (const Change& ch : std::as_const(changes)) {
        c.setPosition(ch.from);
        c.setPosition(ch.to, QTextCursor::KeepAnchor);
        c.setCharFormat(ch.format);
    }
}

} // namespace detail

namespace {

bool isKnownKey(const QString& k)
{
    return formats::inlineKeys().contains(k) || formats::lineKeys().contains(k) || formats::passiveKeys().contains(k);
}

/// Quill treats `false` as "remove" for its formats.
AttributeMap falseToNull(const AttributeMap& attrs)
{
    AttributeMap out = attrs;
    for (auto it = out.begin(); it != out.end(); ++it) {
        if (it.value() == QJsonValue(false) && isKnownKey(it.key()))
            it.value() = QJsonValue(QJsonValue::Null);
    }
    return out;
}

/// Attributes that apply to text and embeds: everything but line formats.
AttributeMap forText(const AttributeMap& attrs)
{
    AttributeMap out = attrs;
    for (const QString& k : formats::lineKeys())
        out.remove(k);
    return out;
}

/// Attributes that apply to a newline: everything but inline and embed formats.
AttributeMap forNewline(const AttributeMap& attrs)
{
    AttributeMap out = attrs;
    for (const QString& k : formats::inlineKeys())
        out.remove(k);
    for (const QString& k : formats::passiveKeys())
        out.remove(k);
    return out;
}

void appendSplit(Delta& out, const QString& text, const AttributeMap& attrs, bool insert)
{
    const auto put = [&](qsizetype from, qsizetype length, const AttributeMap& a) {
        if (insert)
            out.insert(text.mid(from, length), a);
        else
            out.retain(length, a);
    };
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i) != QLatin1Char('\n'))
            continue;
        if (i > start)
            put(start, i - start, forText(attrs));
        put(i, 1, forNewline(attrs));
        start = i + 1;
    }
    if (start < text.size())
        put(start, text.size() - start, forText(attrs));
}

/// Quill's editing rules on top of Delta semantics: inline formats apply to
/// text and embeds only, line formats to newlines only, `false` removes a
/// known format, null and `false` are dropped from inserts, and inserted text is normalised to what QTextDocument can
/// hold. Operations past the end of the document are dropped.
Delta normalizeChange(const Delta& contents, const Delta& change)
{
    Delta out;
    OpIterator base(contents.ops());
    for (const Op& op : change.ops()) {
        AttributeMap attrs = falseToNull(op.attributes());
        if (op.isInsert()) {
            // A removal marker means nothing on inserted content.
            for (auto it = attrs.begin(); it != attrs.end();)
                it = it.value().isNull() ? attrs.erase(it) : std::next(it);
            if (op.isEmbed())
                out.insert(op.embed(), forText(attrs));
            else
                appendSplit(out, detail::normalizeText(op.text()), attrs, true);
            continue;
        }
        qsizetype n = op.length();
        if (op.isDelete() || attrs.isEmpty()) {
            // Plain retains and deletes keep their length (cut at the end of
            // the document); skip without building the ops.
            const qsizetype skipped = base.skip(n);
            if (op.isDelete())
                out.remove(skipped);
            else
                out.retain(skipped);
            continue;
        }
        while (n > 0 && base.hasNext()) {
            const Op piece = base.next(n);
            n -= piece.length();
            if (op.isDelete())
                out.remove(piece.length());
            else if (attrs.isEmpty())
                out.retain(piece.length());
            else if (piece.isEmbed())
                out.retain(1, forText(attrs));
            else
                appendSplit(out, piece.text(), attrs, false);
        }
    }
    return out.chop();
}

/// Rewrites `change` so that QTextDocument can apply it: Qt cannot delete
/// the final paragraph separator or insert after it. Everything the change
/// does at or after the final "\n" is expressed as inserts before it plus an
/// attribute change on it, which yields the same `target`.
Delta qtApplicable(const Delta& contents, const Delta& change, const Delta& target)
{
    const qsizetype last = contents.length() - 1; // position of the final "\n"
    Delta out;
    qsizetype basePos = 0;
    qsizetype newPos = 0;
    OpIterator it(change.ops());
    while (it.hasNext() && basePos < last) {
        if (it.peekType() == Op::Type::Insert) {
            const Op op = it.next();
            newPos += op.length();
            out.push(op);
            continue;
        }
        const Op op = it.next(std::min(it.peekLength(), last - basePos));
        basePos += op.length();
        if (op.isRetain())
            newPos += op.length();
        out.push(op);
    }
    if (!it.hasNext())
        return out.chop();

    const AttributeMap finalAttrs = detail::lastAttributes(contents);
    const Delta rest = target.slice(newPos);
    if (rest.isEmpty()) {
        // The change deleted the final "\n" and the target ends with the
        // previous one: delete that one instead, the final separator takes
        // its attributes.
        return out
            .compose(Delta()
                         .retain(newPos - 1)
                         .remove(1)
                         .retain(1, attributes::diff(finalAttrs, detail::lastAttributes(target))))
            .chop();
    }
    out = out.concat(rest.slice(0, rest.length() - 1)); // rest ends with "\n"
    out.retain(1, attributes::diff(finalAttrs, detail::lastAttributes(rest)));
    return out.chop();
}

bool endsWithNewline(const Delta& d)
{
    return !d.ops().isEmpty() && d.ops().constLast().isTextInsert()
           && d.ops().constLast().text().endsWith(QLatin1Char('\n'));
}

/// Hands out consecutive ranges of a Delta in increasing order without
/// re-walking it from the start.
class DeltaWalker
{
public:
    explicit DeltaWalker(const Delta& d) : m_it(d.ops()) {}

    Delta take(qsizetype from, qsizetype length)
    {
        while (m_pos < from && m_it.hasNext())
            m_pos += m_it.next(from - m_pos).length();
        Delta out;
        while (length > 0 && m_it.hasNext()) {
            const Op op = m_it.next(length);
            length -= op.length();
            m_pos += op.length();
            out.push(op);
        }
        return out;
    }

private:
    OpIterator m_it;
    qsizetype m_pos = 0;
};

void insertPlain(QTextCursor& c, const QString& text)
{
    qsizetype start = 0;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        if (i < text.size() && text.at(i) != QLatin1Char('\n'))
            continue;
        if (i > start)
            c.insertText(text.mid(start, i - start), QTextCharFormat());
        if (i < text.size())
            c.insertBlock();
        start = i + 1;
    }
}

struct Span
{
    int from;
    int to; // exclusive; from == to marks the line containing `from`
};

void restyleSpans(QTextDocument* doc, const QList<Span>& spans, const Delta& target, const StyleOptions& style)
{
    // Block number ranges, merged when adjacent so list regrouping sees runs.
    QList<std::pair<int, int>> ranges;
    const int lastPos = doc->characterCount() - 1;
    for (const Span& s : spans) {
        const int from = std::clamp(s.from, 0, lastPos);
        const int to = std::clamp(std::max(s.from, s.to - 1), 0, lastPos);
        const int first = doc->findBlock(from).blockNumber();
        const int last = doc->findBlock(to).blockNumber();
        if (!ranges.isEmpty() && first <= ranges.last().second + 1)
            ranges.last().second = std::max(ranges.last().second, last);
        else
            ranges.append({first, last});
    }
    DeltaWalker walker(target);
    for (const auto& [first, last] : ranges) {
        const QTextBlock firstBlock = doc->findBlockByNumber(first);
        QTextBlock b = firstBlock;
        for (int n = first; n <= last && b.isValid(); ++n, b = b.next())
            detail::restyleBlock(b, walker.take(b.position(), b.length()), style);
        DeltaDocument::regroupLists(firstBlock, doc->findBlockByNumber(last), style);
        DeltaDocument::updateBlockSpacing(firstBlock, doc->findBlockByNumber(last), style);
    }
}

} // namespace

Delta DeltaDocument::apply(QTextDocument* doc, const Delta& change)
{
    return apply(doc, toDelta(doc), change);
}

Delta DeltaDocument::apply(QTextDocument* doc, const Delta& contents, const Delta& changeIn)
{
    return detail::applyChange(doc, contents, changeIn, nullptr);
}

Delta detail::applyChange(QTextDocument* doc, const Delta& contents, const Delta& changeIn, bool* exact)
{
    const StyleOptions style = DeltaDocument::documentStyle(doc);
    const Delta change = normalizeChange(contents, changeIn);
    Delta target = contents.compose(change);
    const bool newline = !endsWithNewline(target);
    if (newline)
        target.insert(QStringLiteral("\n"));
    if (exact)
        *exact = !newline && change == changeIn;
    const Delta qtChange = qtApplicable(contents, change, target);
    if (qtChange.isEmpty())
        return target;
    if (!EmbedRegistry::registryOf(doc))
        EmbedRegistry::shared()->attachTo(doc);

    QTextCursor c(doc);
    c.beginEditBlock();
    QList<Span> spans;
    int pos = 0;
    for (const Op& op : qtChange.ops()) {
        const int n = int(op.length());
        if (op.isRetain()) {
            if (op.hasAttributes())
                spans.append({pos, pos + n});
            pos += n;
        } else if (op.isDelete()) {
            c.setPosition(pos);
            c.setPosition(pos + n, QTextCursor::KeepAnchor);
            c.removeSelectedText();
            spans.append({pos, pos});
        } else {
            c.setPosition(pos);
            if (op.isEmbed()) {
                const QTextCharFormat f = detail::embedCharFormat(op.embed(), op.attributes(), {}, style);
                if (f.isImageFormat()) {
                    detail::registerImageResource(doc, f.toImageFormat().name());
                    c.insertImage(f.toImageFormat());
                } else {
                    c.insertText(QString(QChar::ObjectReplacementCharacter), f);
                }
            } else {
                insertPlain(c, op.text());
            }
            // Include the line after the insert: a split leaves its remainder there.
            spans.append({pos, pos + n + 1});
            pos += n;
        }
    }
    if (doc->characterCount() != target.length()) {
        qWarning("quill: applying a change produced %d characters instead of %lld", doc->characterCount(),
                 static_cast<long long>(target.length()));
    } else {
        restyleSpans(doc, spans, target, style);
    }
    c.endEditBlock();
    return target;
}

} // namespace quill
