// QuillEditor: Quill's editor API on top of QTextEdit (milestone M5).
//
// The Delta of the document is kept in `contents` and updated from
// QTextDocument::contentsChange: the changed lines are exported again and
// diffed against the same lines of the old contents, which yields Quill-shaped
// change Deltas for every kind of edit (typing, Qt's own editing, undo, API
// calls) without exporting the whole document on each keystroke.

#include "QuillEditor_p.h"

#include "DocumentEdit.h"
#include "quill/DeltaDocument.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QImageReader>
#include <QJsonArray>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <QToolTip>
#include <QHelpEvent>

namespace quill {

namespace {

QString key(const char* k) { return QString::fromLatin1(k); }

constexpr char kDeltaMime[] = "application/x-quill-delta+json";

/// Quill's combineFormats: keys present in both survive; differing values
/// are collected in an array.
AttributeMap combineFormats(const AttributeMap& formats, const AttributeMap& combined)
{
    AttributeMap merged;
    for (auto it = combined.begin(); it != combined.end(); ++it) {
        const QJsonValue v = formats.value(it.key());
        if (v.isUndefined() || v.isNull())
            continue;
        if (it.value() == v) {
            merged.insert(it.key(), v);
        } else if (it.value().isArray()) {
            QJsonArray a = it.value().toArray();
            if (!a.contains(v))
                a.append(v);
            merged.insert(it.key(), a);
        } else {
            merged.insert(it.key(), QJsonArray{it.value(), v});
        }
    }
    return merged;
}

AttributeMap combineAll(const QList<AttributeMap>& list)
{
    if (list.isEmpty())
        return {};
    AttributeMap formats = list.first();
    for (qsizetype i = 1; i < list.size() && !formats.isEmpty(); ++i)
        formats = combineFormats(list.at(i), formats);
    return formats;
}

AttributeMap merged(AttributeMap a, const AttributeMap& b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

} // namespace

// --- Private ---------------------------------------------------------------------

StyleOptions QuillEditorPrivate::docStyle() const { return DeltaDocument::documentStyle(q->document()); }

int QuillEditorPrivate::clampIndex(int index) const
{
    return std::clamp(index, 0, int(contents.length()) - 1);
}

QuillEditorPrivate::Line QuillEditorPrivate::lineAt(int index) const
{
    const QTextBlock block = q->document()->findBlock(clampIndex(index));
    Line line;
    line.start = block.position();
    line.end = block.position() + block.length() - 1;
    line.attrs = inlineAt(line.end);
    line.text = block.text();
    return line;
}

AttributeMap QuillEditorPrivate::inlineAt(int pos) const
{
    const Delta piece = contents.slice(pos, pos + 1);
    return piece.ops().isEmpty() ? AttributeMap() : piece.ops().first().attributes();
}

AttributeMap QuillEditorPrivate::styledFor(const QTextBlock& block) const
{
    const QTextBlockFormat bf = block.blockFormat();
    if (bf.hasProperty(prop::BlockAttributes))
        return bf.property(prop::BlockAttributes).toJsonObject();
    return DeltaDocument::lineAttributes(block, docStyle());
}

AttributeMap QuillEditorPrivate::typingAttributes() const
{
    const QTextCursor c = q->textCursor();
    const QTextCharFormat f = c.charFormat();
    if (f.hasProperty(prop::EmbedType) || f.isImageFormat())
        return detail::inlineSubset(f.property(prop::InlineAttributes).toJsonObject());
    return DeltaDocument::inlineAttributes(f, styledFor(c.block()), docStyle());
}

AttributeMap QuillEditorPrivate::resolveLine(const AttributeMap& current, const AttributeMap& requested) const
{
    AttributeMap out;
    for (auto it = requested.begin(); it != requested.end(); ++it) {
        QJsonValue v = it.value();
        if (v == QJsonValue(false))
            v = QJsonValue(QJsonValue::Null);
        if (it.key() == QLatin1String("indent") && !v.isNull()) {
            int level = 0;
            const QString s = v.toString();
            if (s == QLatin1String("+1") || s == QLatin1String("-1"))
                level = formats::indentLevel(current) + (s == QLatin1String("+1") ? 1 : -1);
            else
                level = formats::indentLevel(AttributeMap{{it.key(), v}});
            level = std::clamp(level, 0, 8);
            v = level > 0 ? QJsonValue(level) : QJsonValue(QJsonValue::Null);
        }
        out.insert(it.key(), v);
    }
    // flutter_quill's exclusive block formats.
    static const QStringList exclusive{key("list"), key("blockquote"), key("code-block")};
    for (const QString& k : exclusive) {
        if (out.value(k).isNull() || out.value(k).isUndefined())
            continue;
        for (const QString& other : exclusive) {
            if (other != k && current.contains(other) && !out.contains(other))
                out.insert(other, QJsonValue(QJsonValue::Null));
        }
    }
    return out;
}

Delta QuillEditorPrivate::lineFormatChange(int index, int length, const AttributeMap& attrs) const
{
    Delta change;
    index = clampIndex(index);
    const int to = index + std::max(length, 1);
    int pos = 0;
    for (QTextBlock b = q->document()->findBlock(index); b.isValid() && b.position() < to; b = b.next()) {
        const int nl = b.position() + b.length() - 1;
        change.retain(nl - pos);
        change.retain(1, resolveLine(inlineAt(nl), attrs));
        pos = nl + 1;
    }
    return change.chop();
}

Delta QuillEditorPrivate::modify(Source src, const std::function<Delta()>& fn)
{
    if (src == Source::User && q->isReadOnly())
        return {};
    const Delta old = contents;
    const qsizetype logStart = log.size();
    const Source savedSource = source;
    ++depth;
    source = src;
    const Delta input = fn();
    --depth;
    source = savedSource;

    Delta tracked;
    for (qsizetype i = logStart; i < log.size(); ++i)
        tracked = tracked.compose(log.at(i));
    Delta change = tracked;
    if (!input.isEmpty()) {
        try {
            if (old.compose(input) == contents)
                change = input;
        } catch (const DeltaError&) {
        }
    }
    if (depth == 0) {
        log.clear();
        if (contents != old && src != Source::Silent) {
            Q_EMIT q->textChange(change, old, src);
            Q_EMIT q->editorChange();
        }
        syncSelection();
    }
    return change;
}

void QuillEditorPrivate::apply(const Delta& change)
{
    // Qt's contentsChange range is unreliable for an edit block whose parts
    // are not in document order (see onContentsChange), and apply() restyles
    // and regroups lists in several passes. The result is known anyway: it
    // is the target apply() computed.
    const Delta old = contents;
    Delta target;
    suppress = true;
    try {
        target = DeltaDocument::apply(q->document(), contents, change);
    } catch (const DeltaError& e) {
        qWarning("quill: cannot apply change: %s", e.what());
        target = DeltaDocument::toDelta(q->document());
    }
    suppress = false;
    if (verify) {
        const Delta actual = DeltaDocument::toDelta(q->document());
        if (actual != target) {
            ++failures;
            qWarning("quill: applied change does not export as expected\n  change: %s\n  expected: %s\n  actual: %s",
                     change.toJson().constData(), target.toJson().constData(), actual.toJson().constData());
            target = actual;
        }
    }
    if (target == old)
        return;
    contents = target;
    Delta applied;
    try {
        if (old.compose(change) == target)
            applied = change;
    } catch (const DeltaError&) {
    }
    log.append(applied.isEmpty() ? old.diff(target) : applied);
}

void QuillEditorPrivate::onContentsChange(int pos, int removed, int added)
{
    QTextDocument* doc = q->document();
    // Undo and redo replay a group of commands in reverse order, for which
    // QTextDocument under-reports the end of the changed range (its
    // documentChange() moves the start back without growing the length).
    // They are recognised by the undo stack: undo removes a step, redo adds
    // one without announcing a new command (merged typing adds none).
    const int undoSteps = doc->availableUndoSteps();
    const int redoSteps = doc->availableRedoSteps();
    const bool undoRedo = undoSteps < lastUndoSteps || redoSteps > lastRedoSteps
                          || (!commandAdded && (undoSteps > lastUndoSteps || redoSteps < lastRedoSteps));
    lastUndoSteps = undoSteps;
    lastRedoSteps = redoSteps;
    commandAdded = false;
    if (suppress)
        return;
    if (undoRedo) {
        resync(Source::User);
        return;
    }
    const qsizetype oldLength = contents.length();
    const qsizetype newLength = doc->characterCount();
    pos = int(std::clamp<qsizetype>(pos, 0, std::min(oldLength, newLength) - 1));
    // The unchanged suffix, then whole lines: inline attributes depend on
    // the line they are on.
    // Qt's counts are not always consistent with each other (grouped undo
    // commands); the smaller tail is the safe one.
    qsizetype tail = std::max<qsizetype>(0, std::min(newLength - (pos + added), oldLength - (pos + removed)));
    tail = std::min({tail, newLength - pos, oldLength - pos});
    const int start = doc->findBlock(pos).position();
    qsizetype newEnd = newLength - tail;
    qsizetype oldEnd = oldLength - tail;
    const QTextBlock endBlock = doc->findBlock(int(std::max<qsizetype>(newEnd - 1, start)));
    const qsizetype extra = std::max<qsizetype>(0, endBlock.position() + endBlock.length() - newEnd);
    newEnd += extra;
    oldEnd += extra;

    const Delta newSlice = DeltaDocument::toDelta(doc, start, int(newEnd));
    const Delta oldSlice = contents.slice(start, oldEnd);
    Delta change;
    if (newSlice != oldSlice) {
        // The selection before and after the edit resolves ambiguous diffs
        // ("aa" -> "aaa") the way Quill does. lastRange is still the old one:
        // Qt moves the cursor after reporting the change.
        std::optional<fastdiff::CursorInfo> hint;
        const QTextCursor c = q->textCursor();
        const Range now{std::min(c.anchor(), c.position()), std::abs(c.anchor() - c.position())};
        const auto inside = [&](const Range& r, qsizetype length) {
            return r.index >= start && r.index + r.length <= start + length;
        };
        if (depth == 0 && inside(lastRange, oldSlice.length()) && inside(now, newSlice.length()))
            hint = fastdiff::CursorInfo{{lastRange.index - start, lastRange.length},
                                        fastdiff::Range{now.index - start, now.length}};
        const Delta diff = oldSlice.diff(newSlice, hint);
        change = Delta().retain(start).concat(diff).chop();
    }
    const Delta old = contents;
    if (!change.isEmpty())
        contents = contents.compose(change);
    if (verify) {
        const Delta full = DeltaDocument::toDelta(doc);
        if (full != contents) {
            ++failures;
            qWarning("quill: tracked contents drifted after contentsChange(%d, -%d, +%d)\n  change: %s\n  tracked: %s\n  actual: %s",
                     pos, removed, added, change.toJson().constData(), contents.toJson().constData(),
                     full.toJson().constData());
            change = old.diff(full);
            contents = full;
        }
    }
    if (change.isEmpty())
        return;
    if (depth > 0) {
        log.append(change);
    } else {
        Q_EMIT q->textChange(change, old, Source::User);
        Q_EMIT q->editorChange();
    }
}

void QuillEditorPrivate::resync(Source src)
{
    const Delta full = DeltaDocument::toDelta(q->document());
    if (full == contents)
        return;
    const Delta old = contents;
    const Delta change = old.diff(full);
    contents = full;
    if (depth > 0) {
        log.append(change);
    } else if (src != Source::Silent) {
        Q_EMIT q->textChange(change, old, src);
        Q_EMIT q->editorChange();
    }
}

void QuillEditorPrivate::syncSelection()
{
    const QTextCursor c = q->textCursor();
    const Range r{std::min(c.anchor(), c.position()), std::abs(c.anchor() - c.position())};
    if (r == lastRange)
        return;
    const Range old = lastRange;
    lastRange = r;
    const Source s = depth > 0 ? Source::Silent : selectionSource;
    if (s != Source::Silent) {
        Q_EMIT q->selectionChange(r, old, s);
        Q_EMIT q->editorChange();
    }
}

void QuillEditorPrivate::sanitizeTypingFormat()
{
    if (sanitizing || q->isReadOnly())
        return;
    QTextCursor c = q->textCursor();
    if (c.hasSelection())
        return;
    const QTextCharFormat f = c.charFormat();
    const QTextBlock block = c.block();
    const bool embed = f.hasProperty(prop::EmbedType) || f.isImageFormat();
    bool changed = embed;
    AttributeMap attrs = typingAttributes();
    // A link only extends while typing inside it (flutter_quill), not at its ends.
    if (attrs.contains(key("link"))) {
        const QString href = attrs.value(key("link")).toString();
        const auto linkAt = [&](int pos) {
            if (pos < block.position() || pos >= block.position() + block.length() - 1)
                return false;
            return inlineAt(pos).value(key("link")).toString() == href;
        };
        if (!linkAt(c.position() - 1) || !linkAt(c.position())) {
            attrs.remove(key("link"));
            changed = true;
        }
    }
    if (!changed)
        return;
    sanitizing = true;
    q->setCurrentCharFormat(formats::charFormat(attrs, styledFor(block), docStyle()));
    sanitizing = false;
}

void QuillEditorPrivate::reload()
{
    const Delta current = contents;
    const Range sel = lastRange;
    suppress = true;
    LoadOptions options;
    options.style = style;
    options.embeds = embeds;
    DeltaDocument::load(q->document(), current, options);
    suppress = false;
    contents = DeltaDocument::toDelta(q->document());
    if (contents != current)
        qWarning("quill: re-rendering changed the document");
    const Source saved = selectionSource;
    selectionSource = Source::Silent;
    QTextCursor c = q->textCursor();
    c.setPosition(clampIndex(sel.index));
    c.setPosition(clampIndex(sel.index + sel.length), QTextCursor::KeepAnchor);
    q->setTextCursor(c);
    selectionSource = saved;
}

Delta QuillEditorPrivate::htmlToDelta(const QString& html) const
{
    QTextDocument scratch;
    scratch.setDefaultFont(q->document()->defaultFont());
    scratch.setHtml(html);
    ExportOptions options;
    options.style = docStyle();
    const Delta raw = DeltaDocument::toDelta(&scratch, options);
    Delta out;
    for (Op op : raw.ops()) {
        AttributeMap attrs = op.attributes();
        for (const QString& k : pasteIgnored)
            attrs.remove(k);
        op.setAttributes(attrs);
        out.push(op);
    }
    // Quill: a trailing plain newline is the end of the pasted fragment, not a line.
    if (!out.isEmpty() && detail::lastAttributes(out).isEmpty())
        out = out.slice(0, out.length() - 1);
    return out;
}

Delta QuillEditorPrivate::plainTextChange(const Range& r, const QString& textIn) const
{
    // flutter_quill: pasted text takes the style of the character before it,
    // without extending a link that ends there; its newlines keep the style
    // of the line they split.
    const QString text = detail::normalizeText(textIn);
    const Line first = lineAt(r.index);
    AttributeMap inlineAttrs;
    if (r.index > first.start) {
        const Delta prev = contents.slice(r.index - 1, r.index);
        inlineAttrs = prev.ops().first().isEmbed() ? detail::inlineSubset(prev.ops().first().attributes())
                                                    : prev.ops().first().attributes();
        const QString link = inlineAttrs.value(key("link")).toString();
        if (!link.isEmpty() && inlineAt(r.index + r.length).value(key("link")).toString() != link)
            inlineAttrs.remove(key("link"));
    }
    const AttributeMap lineAttrs = lineAt(r.index + r.length).attrs;
    Delta change = Delta().retain(r.index).remove(r.length);
    const QStringList parts = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (i > 0)
            change.insert(QStringLiteral("\n"), lineAttrs);
        change.insert(parts.at(i), inlineAttrs);
    }
    return change;
}

// --- QuillEditor -------------------------------------------------------------------

QuillEditor::QuillEditor(QWidget* parent) : QTextEdit(parent), d(std::make_unique<QuillEditorPrivate>(this))
{
    d->style.codeBlockBackgroundOnBlocks = false;
    d->pasteIgnored = {key("font"), key("size"), key("color"), key("background")};
    d->verify = qEnvironmentVariableIntValue("QUILLQT_VERIFY") == 1;
    d->embeds = new EmbedRegistry(this);
    d->embeds->setRemoteImagesEnabled(true);
    viewport()->setMouseTracking(true);

    connect(document(), &QTextDocument::contentsChange, this,
            [this](int pos, int removed, int added) { d->onContentsChange(pos, removed, added); });
    connect(document(), &QTextDocument::undoCommandAdded, this, [this] { d->commandAdded = true; });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        d->syncSelection();
        d->sanitizeTypingFormat();
    });
    connect(this, &QTextEdit::selectionChanged, this, [this] { d->syncSelection(); });

    setContents(Delta().insert(QStringLiteral("\n")), Source::Silent);
}

QuillEditor::~QuillEditor()
{
    disconnect(document(), nullptr, this, nullptr);
    d->suppress = true;
}

Delta QuillEditor::getContents(int index, int length) const
{
    index = std::max(index, 0);
    return d->contents.slice(index, index + qsizetype(std::max(length, 0)));
}

Delta QuillEditor::setContents(const Delta& document, Source source)
{
    return d->modify(source, [&] {
        const Delta old = d->contents;
        d->suppress = true;
        LoadOptions options;
        options.style = d->style;
        options.embeds = d->embeds;
        DeltaDocument::load(this->document(), document, options);
        d->suppress = false;
        d->contents = DeltaDocument::toDelta(this->document());
        // Quill's setContents change: insert the new document, delete the old.
        Delta change = d->contents;
        change.remove(old.length());
        d->log.append(change);
        return change;
    });
}

Delta QuillEditor::updateContents(const Delta& change, Source source)
{
    return d->modify(source, [&] {
        d->apply(change);
        return change;
    });
}

QString QuillEditor::getText(int index, int length) const
{
    QString text;
    const Delta part = getContents(index, length);
    for (const Op& op : part.ops())
        text += op.isEmbed() ? QString(QChar::ObjectReplacementCharacter) : op.text();
    return text;
}

int QuillEditor::getLength() const { return int(d->contents.length()); }

Delta QuillEditor::insertText(int index, const QString& textIn, const AttributeMap& attributes, Source source)
{
    return d->modify(source, [&] {
        index = d->clampIndex(index);
        const QString text = detail::normalizeText(textIn);
        AttributeMap inlineAttrs = attributes;
        AttributeMap lineAttrs = d->lineAt(index).attrs;
        for (const QString& k : formats::lineKeys()) {
            inlineAttrs.remove(k);
            if (attributes.contains(k))
                lineAttrs = attributes::compose(lineAttrs, AttributeMap{{k, attributes.value(k)}});
        }
        Delta change = Delta().retain(index);
        const QStringList parts = text.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (i > 0)
                change.insert(QStringLiteral("\n"), lineAttrs);
            change.insert(parts.at(i), inlineAttrs);
        }
        d->apply(change);
        return Delta().retain(index).insert(text, attributes);
    });
}

Delta QuillEditor::insertEmbed(int index, const QString& type, const QJsonValue& value, Source source)
{
    return d->modify(source, [&] {
        const Delta change = Delta().retain(d->clampIndex(index)).insert(QJsonObject{{type, value}});
        d->apply(change);
        return change;
    });
}

Delta QuillEditor::insertBlockEmbed(int index, const QString& type, const QJsonValue& value, Source source)
{
    return d->modify(source, [&] {
        const Delta change = detail::embedInsertChange(d->contents, d->clampIndex(index), 0,
                                                       QJsonObject{{type, value}}, true);
        d->apply(change);
        return change;
    });
}

EmbedRegistry* QuillEditor::embeds() const { return d->embeds; }

Delta QuillEditor::deleteText(int index, int length, Source source)
{
    return d->modify(source, [&] {
        const Delta change = Delta().retain(d->clampIndex(index)).remove(length);
        d->apply(change);
        return change;
    });
}

Delta QuillEditor::format(const QString& name, const QJsonValue& value, Source source)
{
    const Range r = getSelection();
    if (formats::lineKeys().contains(name))
        return formatLine(r.index, r.length, AttributeMap{{name, value}}, source);
    if (r.length > 0)
        return formatText(r.index, r.length, AttributeMap{{name, value}}, source);
    if (source == Source::User && isReadOnly())
        return {};
    // Collapsed: the format applies to the text typed next.
    const QJsonValue v = value == QJsonValue(false) ? QJsonValue(QJsonValue::Null) : value;
    const AttributeMap attrs = attributes::compose(d->typingAttributes(), AttributeMap{{name, v}});
    d->sanitizing = true;
    setCurrentCharFormat(formats::charFormat(attrs, d->styledFor(textCursor().block()), d->docStyle()));
    d->sanitizing = false;
    return {};
}

Delta QuillEditor::formatText(int index, int length, const AttributeMap& attributes, Source source)
{
    return d->modify(source, [&] {
        const Delta change = Delta().retain(d->clampIndex(index)).retain(length, attributes);
        d->apply(change);
        return change;
    });
}

Delta QuillEditor::formatLine(int index, int length, const AttributeMap& attributes, Source source)
{
    return d->modify(source, [&] {
        const Delta change = d->lineFormatChange(index, length, attributes);
        d->apply(change);
        return change;
    });
}

Delta QuillEditor::removeFormat(int index, int length, Source source)
{
    return d->modify(source, [&] {
        index = d->clampIndex(index);
        const int end = d->clampIndex(index + std::max(length, 0));
        const int segmentEnd = d->lineAt(end).end + 1;
        const Delta segment = d->contents.slice(index, segmentEnd);
        // Target: no formats in the range and on the newline ending the last
        // line; the rest of that line keeps its formats (Quill).
        Delta target;
        int pos = index;
        for (const Op& op : segment.ops()) {
            const int n = int(op.length());
            if (op.isEmbed()) {
                target.insert(op.embed(), op.attributes());
            } else {
                const QString& t = op.text();
                for (int i = 0; i < n; ++i) {
                    const int p = pos + i;
                    const bool keep = p >= index + length && p != segmentEnd - 1;
                    target.insert(QString(t.at(i)), keep ? op.attributes() : AttributeMap());
                }
            }
            pos += n;
        }
        const Delta change = Delta().retain(index).concat(segment.diff(target)).chop();
        d->apply(change);
        return change;
    });
}

AttributeMap QuillEditor::getFormat(int index, int length) const
{
    index = d->clampIndex(index);
    if (length <= 0) {
        const auto line = d->lineAt(index);
        AttributeMap leaf;
        if (index > line.start)
            leaf = d->inlineAt(index - 1);
        else if (index < line.end)
            leaf = d->inlineAt(index);
        return merged(line.attrs, leaf);
    }
    const int end = std::min(index + length, getLength());
    QList<AttributeMap> lines;
    for (QTextBlock b = document()->findBlock(index); b.isValid() && b.position() < end; b = b.next())
        lines.append(d->inlineAt(b.position() + b.length() - 1));
    QList<AttributeMap> leaves;
    const Delta part = d->contents.slice(index, end);
    for (const Op& op : part.ops()) {
        if (op.isEmbed() || op.text().contains(QRegularExpression(QStringLiteral("[^\\n]"))))
            leaves.append(op.attributes());
    }
    return merged(combineAll(lines), combineAll(leaves));
}

AttributeMap QuillEditor::getFormat() const
{
    const Range r = getSelection();
    if (r.length > 0)
        return getFormat(r.index, r.length);
    return merged(d->lineAt(r.index).attrs, d->typingAttributes());
}

Range QuillEditor::getSelection() const
{
    const QTextCursor c = textCursor();
    return {std::min(c.anchor(), c.position()), std::abs(c.anchor() - c.position())};
}

void QuillEditor::setSelection(int index, int length, Source source)
{
    const Source saved = d->selectionSource;
    d->selectionSource = source;
    QTextCursor c = textCursor();
    const int from = d->clampIndex(index);
    c.setPosition(from);
    c.setPosition(d->clampIndex(from + std::max(length, 0)), QTextCursor::KeepAnchor);
    setTextCursor(c);
    d->syncSelection();
    d->selectionSource = saved;
}

StyleOptions QuillEditor::styleOptions() const { return d->style; }

void QuillEditor::setStyleOptions(const StyleOptions& style)
{
    d->style = style;
    d->style.codeBlockBackgroundOnBlocks = false; // the editor paints its own panel
    d->reload();
    viewport()->update();
}

QStringList QuillEditor::pasteIgnoredAttributes() const { return d->pasteIgnored; }
void QuillEditor::setPasteIgnoredAttributes(const QStringList& keys) { d->pasteIgnored = keys; }

void QuillEditor::setVerifyChanges(bool on) { d->verify = on; }
int QuillEditor::verificationFailures() const { return d->failures; }

QString QuillEditor::imageSourceFor(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

// --- Mouse ---------------------------------------------------------------------------

namespace {
QPointF documentPos(const QTextEdit* e, const QPoint& p)
{
    return QPointF(p) + QPointF(e->horizontalScrollBar()->value(), e->verticalScrollBar()->value());
}
} // namespace

int QuillEditor::embedAt(const QPoint& pos) const
{
    QTextDocument* doc = document();
    const QPointF p = documentPos(this, pos);
    const int hit = cursorForPosition(pos).position();
    for (const int at : {hit - 1, hit}) {
        if (at < 0 || doc->characterAt(at) != QChar::ObjectReplacementCharacter)
            continue;
        const QTextBlock b = doc->findBlock(at);
        const int rel = at - b.position();
        const QTextLine line = b.layout()->lineForTextPosition(rel);
        if (!line.isValid())
            continue;
        const QRectF br = doc->documentLayout()->blockBoundingRect(b);
        const qreal x1 = line.cursorToX(rel), x2 = line.cursorToX(rel + 1);
        const QRectF r(br.left() + std::min(x1, x2), br.top() + line.y(), std::abs(x2 - x1), line.height());
        if (!r.contains(p))
            continue;
        QTextCursor c(doc);
        c.setPosition(at + 1);
        const QTextCharFormat f = c.charFormat();
        if (f.hasProperty(prop::EmbedType) || f.isImageFormat())
            return at;
    }
    return -1;
}

std::pair<EmbedRenderer*, EmbedContext> QuillEditorPrivate::embedInfo(int index) const
{
    QTextCursor c(q->document());
    c.setPosition(index + 1);
    EmbedRegistry* registry = EmbedRegistry::registryOf(q->document());
    if (!registry)
        registry = embeds;
    const EmbedContext ctx = registry->context(q->document(), index, c.charFormat());
    return {registry->rendererFor(ctx.key, ctx.data), ctx};
}

void QuillEditorPrivate::activateEmbed(int index)
{
    auto [handler, ctx] = embedInfo(index);
    if (!handler->activate(q, ctx))
        Q_EMIT q->embedActivated(ctx.key, ctx.data, index);
}

void QuillEditor::mouseDoubleClickEvent(QMouseEvent* e)
{
    QTextEdit::mouseDoubleClickEvent(e);
    if (e->button() != Qt::LeftButton)
        return;
    const int at = embedAt(e->pos());
    if (at >= 0 && d->embedInfo(at).first->activatesOnDoubleClick())
        d->activateEmbed(at);
}

bool QuillEditor::viewportEvent(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(e);
        const int at = embedAt(help->pos());
        if (at >= 0) {
            auto [handler, ctx] = d->embedInfo(at);
            const QString tip = handler->toolTip(ctx);
            if (!tip.isEmpty()) {
                QToolTip::showText(help->globalPos(), tip, viewport());
                return true;
            }
        }
    }
    return QTextEdit::viewportEvent(e);
}

void QuillEditor::mousePressEvent(QMouseEvent* e)
{
    d->pressPos = e->pos();
    if (e->button() == Qt::LeftButton && !isReadOnly()) {
        const QTextBlock b = document()->documentLayout()->blockWithMarkerAt(documentPos(this, e->pos()));
        if (b.isValid() && b.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker) {
            // Route the toggle through the API (Qt would toggle on release itself).
            d->markerPressed = true;
            const int nl = b.position() + b.length() - 1;
            const bool checked = d->inlineAt(nl).value(key("list")).toString() == QLatin1String("checked");
            updateContents(Delta().retain(nl).retain(1, AttributeMap{{key("list"), checked ? key("unchecked") : key("checked")}}),
                           Source::User);
            e->accept();
            return;
        }
    }
    QTextEdit::mousePressEvent(e);
}

void QuillEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (d->markerPressed) {
        d->markerPressed = false;
        e->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(e);
    if (e->button() == Qt::LeftButton && (e->pos() - d->pressPos).manhattanLength() < 4
        && (isReadOnly() || (e->modifiers() & Qt::ControlModifier))) {
        const QString href = anchorAt(e->pos());
        if (!href.isEmpty()) {
            Q_EMIT linkActivated(QUrl(href));
            return;
        }
    }
    if (e->button() == Qt::LeftButton && (e->pos() - d->pressPos).manhattanLength() < 4) {
        const int at = embedAt(e->pos());
        if (at >= 0 && !d->embedInfo(at).first->activatesOnDoubleClick())
            d->activateEmbed(at);
    }
}

void QuillEditor::mouseMoveEvent(QMouseEvent* e)
{
    QTextEdit::mouseMoveEvent(e);
    const bool link = !anchorAt(e->pos()).isEmpty() && (isReadOnly() || (e->modifiers() & Qt::ControlModifier));
    const bool marker = !isReadOnly()
                        && document()->documentLayout()->blockWithMarkerAt(documentPos(this, e->pos())).isValid();
    const int embed = embedAt(e->pos());
    const bool clickable = embed >= 0 && !d->embedInfo(embed).first->activatesOnDoubleClick();
    viewport()->setCursor(link || marker || clickable ? Qt::PointingHandCursor : (isReadOnly() ? Qt::ArrowCursor : Qt::IBeamCursor));
}

// --- Decorations ---------------------------------------------------------------------

void QuillEditor::paintEvent(QPaintEvent* e)
{
    {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        const QPointF offset(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
        QTextDocument* doc = document();
        QAbstractTextDocumentLayout* layout = doc->documentLayout();
        const StyleOptions style = d->docStyle();
        const qreal left = doc->documentMargin() + offset.x();
        const qreal width = (doc->textWidth() > 0 ? doc->textWidth() : viewport()->width()) - 2 * doc->documentMargin();
        const auto kind = [](const QTextBlock& b) {
            return BlockKind(b.blockFormat().intProperty(prop::BlockKind));
        };

        QTextBlock b = cursorForPosition(QPoint(0, 0)).block();
        while (kind(b) == BlockKind::CodeBlock && b.previous().isValid() && kind(b.previous()) == BlockKind::CodeBlock)
            b = b.previous();
        QRectF run;
        const auto flush = [&] {
            if (run.isNull())
                return;
            QPainterPath path;
            path.addRoundedRect(run, 3, 3);
            p.fillPath(path, style.codeBlockBackground);
            run = QRectF();
        };
        for (; b.isValid(); b = b.next()) {
            if (!b.isVisible())
                continue;
            const QRectF r = layout->blockBoundingRect(b).translated(offset);
            const qreal indent = b.blockFormat().indent() * doc->indentWidth();
            if (kind(b) == BlockKind::CodeBlock) {
                const QRectF panel(left + indent, r.top(), width - indent, r.height());
                run = run.isNull() ? panel : run.united(panel);
                continue;
            }
            flush();
            if (r.top() > viewport()->height())
                break;
            if (kind(b) == BlockKind::Blockquote) {
                const bool rtl = b.blockFormat().layoutDirection() == Qt::RightToLeft;
                const qreal x = rtl ? left + width - indent - style.blockquoteBarWidth : left + indent;
                p.fillRect(QRectF(x, r.top(), style.blockquoteBarWidth, r.height()), style.blockquoteBarColor);
            }
        }
        flush();
    }
    QTextEdit::paintEvent(e);
}

void QuillEditor::changeEvent(QEvent* e)
{
    QTextEdit::changeEvent(e);
    if (e->type() == QEvent::FontChange && d->style.baseFont.family().isEmpty()
        && d->style.baseFont.families().isEmpty())
        d->reload();
}

// --- Clipboard -------------------------------------------------------------------------

QMimeData* QuillEditor::createMimeDataFromSelection() const
{
    // QTextEdit's own mime data lists a fixed set of formats; copy them.
    const std::unique_ptr<QMimeData> qt(QTextEdit::createMimeDataFromSelection());
    auto* md = new QMimeData;
    for (const QString& format : qt->formats())
        md->setData(format, qt->data(format));
    const Range r = getSelection();
    md->setData(QString::fromLatin1(kDeltaMime), getContents(r.index, r.length).toJson());
    return md;
}

bool QuillEditor::canInsertFromMimeData(const QMimeData* source) const
{
    return source->hasFormat(QString::fromLatin1(kDeltaMime)) || source->hasImage() || source->hasUrls()
           || QTextEdit::canInsertFromMimeData(source);
}

Delta QuillEditorPrivate::pasteChange(const QMimeData* source, const Range& r) const
{
    const bool code = lineAt(r.index).attrs.contains(key("code-block"));
    Delta pasted;
    if (source->hasFormat(QString::fromLatin1(kDeltaMime))) {
        const auto delta = Delta::fromJson(source->data(QString::fromLatin1(kDeltaMime)));
        if (delta && delta->isDocument())
            pasted = *delta;
    }
    if (pasted.isEmpty() && source->hasImage()) {
        const QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull())
            pasted.insert(QJsonObject{{key("image"), q->imageSourceFor(image)}});
    }
    if (pasted.isEmpty() && source->hasUrls()) {
        Delta images;
        bool allImages = true;
        for (const QUrl& url : source->urls()) {
            if (!url.isLocalFile() || QImageReader::imageFormat(url.toLocalFile()).isEmpty()) {
                allImages = false;
                break;
            }
            images.insert(QJsonObject{{key("image"), url.toString()}});
        }
        if (allImages)
            pasted = images;
    }
    if (pasted.isEmpty() && source->hasHtml() && !code)
        pasted = htmlToDelta(source->html());
    if (!pasted.isEmpty())
        return Delta().retain(r.index).remove(r.length).concat(pasted);
    if (source->hasText() && !source->text().isEmpty())
        return plainTextChange(r, source->text());
    return {};
}

void QuillEditor::insertFromMimeData(const QMimeData* source)
{
    if (isReadOnly())
        return;
    const Range r = getSelection();
    const Delta change = d->pasteChange(source, r);
    if (change.isEmpty())
        return;
    const Delta applied = updateContents(change, Source::User);
    setSelection(int(applied.transformPosition(r.index + r.length)), 0, Source::Silent);
    ensureCursorVisible();
}

void QuillEditor::dropEvent(QDropEvent* e)
{
    // Qt removes the dragged text and inserts it again in one edit block,
    // whose changed range it does not report reliably; do it as one change.
    if (isReadOnly() || !canInsertFromMimeData(e->mimeData())) {
        QTextEdit::dropEvent(e);
        return;
    }
    QDragLeaveEvent leave; // clears Qt's drop caret
    QTextEdit::dragLeaveEvent(&leave);

    const int pos = cursorForPosition(e->position().toPoint()).position();
    const Range sel = getSelection();
    const bool internal = e->source() == this || e->source() == viewport();
    const bool move = internal && e->dropAction() == Qt::MoveAction;
    if (internal && sel.length > 0 && pos >= sel.index && pos <= sel.index + sel.length) {
        e->acceptProposedAction(); // dropped onto itself
        return;
    }
    Delta change = d->pasteChange(e->mimeData(), Range{pos, 0});
    if (change.isEmpty()) {
        e->ignore();
        return;
    }
    if (move && sel.length > 0) {
        const Delta removal = Delta().retain(sel.index).remove(sel.length);
        change = removal.compose(removal.transform(change, true));
    }
    const qsizetype before = d->contents.length();
    const Delta applied = updateContents(change, Source::User);
    const int end = int(applied.transformPosition(pos));
    const int inserted = int(d->contents.length() - before) + (move ? sel.length : 0);
    setSelection(std::max(0, end - inserted), inserted, Source::User);
    e->setDropAction(move ? Qt::MoveAction : Qt::CopyAction);
    e->accept();
    setFocus();
}

} // namespace quill
