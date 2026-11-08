// Keyboard behaviour of QuillEditor: Quill's default keyboard bindings and
// flutter_quill's line rules. Every edit goes through updateContents so it
// produces one change Delta and one undo step.

#include "QuillEditor_p.h"

#include "DocumentEdit.h"

#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextBlock>

namespace quill {

namespace {
QString key(const char* k) { return QString::fromLatin1(k); }
const QJsonValue Null(QJsonValue::Null);
} // namespace

void QuillEditor::keyPressEvent(QKeyEvent* e)
{
    if (isReadOnly()) {
        QTextEdit::keyPressEvent(e);
        return;
    }
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    if (e->matches(QKeySequence::Bold)) {
        d->toggle(key("bold"));
        return;
    }
    if (e->matches(QKeySequence::Italic)) {
        d->toggle(key("italic"));
        return;
    }
    if (e->matches(QKeySequence::Underline)) {
        d->toggle(key("underline"));
        return;
    }
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            d->handleEnter(mods & Qt::ShiftModifier);
            ensureCursorVisible();
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (mods == Qt::NoModifier && d->handleBackspace())
            return;
        break;
    case Qt::Key_Tab:
        if ((mods == Qt::NoModifier || mods == Qt::ShiftModifier) && d->handleTab(mods == Qt::ShiftModifier))
            return;
        break;
    case Qt::Key_Backtab:
        if (d->handleTab(true))
            return;
        break;
    case Qt::Key_Space:
        if ((mods & ~Qt::ShiftModifier) == Qt::NoModifier && d->handleSpace())
            return;
        break;
    case Qt::Key_K:
        if (mods == Qt::ControlModifier) {
            Q_EMIT linkRequested();
            return;
        }
        break;
    case Qt::Key_X:
        if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
            d->toggle(key("strike"));
            return;
        }
        break;
    default:
        break;
    }
    QTextEdit::keyPressEvent(e);
}

void QuillEditorPrivate::toggle(const QString& name)
{
    const bool on = q->getFormat().value(name) == QJsonValue(true);
    q->format(name, on ? Null : QJsonValue(true), Source::User);
}

void QuillEditorPrivate::handleEnter(bool plain)
{
    const Range r = q->getSelection();
    const Line line = lineAt(r.index);
    AttributeMap carry = r.length == 0 ? typingAttributes() : inlineAt(r.index);
    carry.remove(key("link"));
    carry.remove(key("code"));

    Delta change;
    if (!plain && r.length == 0) {
        const AttributeMap& a = line.attrs;
        if (line.isEmpty() && !formats::listGroup(a).isEmpty()) {
            // Quill "list empty enter": leave the list.
            q->updateContents(lineFormatChange(r.index, 0, {{key("list"), Null}, {key("indent"), Null}}),
                              Source::User);
            return;
        }
        for (const char* k : {"blockquote", "code-block"}) {
            if (!line.isEmpty() || !a.contains(key(k)))
                continue;
            // flutter_quill AutoExitBlockRule: an empty last line leaves the block.
            const bool nextSame = line.end + 1 < int(contents.length())
                                  && lineAt(line.end + 1).attrs.value(key(k)) == a.value(key(k));
            if (!nextSame) {
                q->updateContents(lineFormatChange(r.index, 0, {{key(k), Null}}), Source::User);
                return;
            }
        }
        if (a.contains(key("header")) && r.index == line.end) {
            // Quill "header enter": the new line is a paragraph.
            change = Delta().retain(r.index).insert(QStringLiteral("\n"), a).retain(1, {{key("header"), Null}});
        } else if (a.value(key("list")) == QJsonValue(key("checked"))) {
            // Quill "checklist enter": the new item is unchecked.
            change = Delta()
                         .retain(r.index)
                         .insert(QStringLiteral("\n"), a)
                         .retain(line.end - r.index)
                         .retain(1, {{key("list"), key("unchecked")}});
        }
    }
    if (change.isEmpty())
        change = Delta().retain(r.index).remove(r.length).insert(QStringLiteral("\n"), line.attrs);
    q->updateContents(change, Source::User);
    q->setSelection(r.index + 1, 0, Source::Silent);
    // Quill carries the inline formats over to the new line.
    sanitizing = true;
    q->setCurrentCharFormat(formats::charFormat(carry, styledFor(q->textCursor().block()), docStyle()));
    sanitizing = false;
}

bool QuillEditorPrivate::handleBackspace()
{
    const Range r = q->getSelection();
    if (r.length != 0)
        return false;
    const Line line = lineAt(r.index);
    if (r.index != line.start)
        return false;
    const AttributeMap& a = line.attrs;
    const auto removeLineFormat = [&](const AttributeMap& f) {
        q->updateContents(lineFormatChange(r.index, 0, f), Source::User);
        return true;
    };
    // Quill "outdent backspace", then block formats before merging lines.
    if (formats::indentLevel(a) > 0)
        return removeLineFormat({{key("indent"), key("-1")}});
    if (!formats::listGroup(a).isEmpty())
        return removeLineFormat({{key("list"), Null}});
    if (a.contains(key("header")))
        return removeLineFormat({{key("header"), Null}});
    for (const char* k : {"blockquote", "code-block"}) {
        if (!a.contains(key(k)))
            continue;
        // Inside a quote or code block, merge with the previous line instead.
        if (r.index == 0 || lineAt(r.index - 1).attrs.value(key(k)) != a.value(key(k)))
            return removeLineFormat({{key(k), Null}});
    }
    if (r.index == 0)
        return true;
    // Quill handleBackspace: the merged line keeps the previous line's
    // format unless the previous line is empty.
    const Line prev = lineAt(r.index - 1);
    Delta change = Delta().retain(r.index - 1).remove(1);
    if (!prev.isEmpty()) {
        const AttributeMap f = attributes::diff(a, prev.attrs);
        if (!f.isEmpty())
            change = change.compose(Delta().retain(line.end - 1).retain(1, f));
    }
    q->updateContents(change, Source::User);
    return true;
}

bool QuillEditorPrivate::handleTab(bool back)
{
    const Range r = q->getSelection();
    const Line first = lineAt(r.index);
    const bool multiLine = lineAt(r.index + r.length).start != first.start;
    if (back) {
        q->updateContents(lineFormatChange(r.index, r.length, {{key("indent"), key("-1")}}), Source::User);
        return true; // never move focus
    }
    if (multiLine || !formats::listGroup(first.attrs).isEmpty()) {
        q->updateContents(lineFormatChange(r.index, r.length, {{key("indent"), key("+1")}}), Source::User);
        return true;
    }
    return false; // QTextEdit inserts "\t" with the typing format
}

bool QuillEditorPrivate::handleSpace()
{
    // Quill "list autofill": "1." / "-" / "*" / "[]" / "[ ]" / "[x]" + space.
    const Range r = q->getSelection();
    if (r.length != 0)
        return false;
    const Line line = lineAt(r.index);
    if (line.attrs.contains(key("code-block")) || line.attrs.contains(key("blockquote")))
        return false;
    const QString prefix = line.text.left(r.index - line.start);
    static const QRegularExpression re(QStringLiteral("^\\s*?(\\d+\\.|-|\\*|\\[ ?\\]|\\[x\\])$"));
    if (!re.match(prefix).hasMatch())
        return false;
    const QString marker = prefix.trimmed();
    QString value = key("ordered");
    if (marker == QLatin1String("[]") || marker == QLatin1String("[ ]"))
        value = key("unchecked");
    else if (marker == QLatin1String("[x]"))
        value = key("checked");
    else if (marker == QLatin1String("-") || marker == QLatin1String("*"))
        value = key("bullet");

    // Insert the space first, so that undo brings back the typed marker.
    q->insertText(r.index, QStringLiteral(" "), typingAttributes(), Source::User);
    const int textLength = line.end - line.start;
    const int prefixLength = int(prefix.size());
    const Delta change = Delta()
                             .retain(line.start)
                             .remove(prefixLength + 1)
                             .retain(textLength - prefixLength)
                             .retain(1, resolveLine(line.attrs, {{key("list"), value}}));
    q->updateContents(change, Source::User);
    q->setSelection(line.start, 0, Source::Silent);
    return true;
}

} // namespace quill
