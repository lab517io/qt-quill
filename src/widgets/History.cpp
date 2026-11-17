// Port of flutter_quill's lib/src/document/history.dart and Quill.js's
// modules/history.ts (selection restore).

#include "quill/History.h"

#include "quill/Formats.h"
#include "quill/QuillEditor.h"

#include <QDateTime>

namespace quill {

namespace {

std::optional<Range> transformRange(const std::optional<Range>& range, const Delta& change)
{
    if (!range)
        return range;
    const int start = int(change.transformPosition(range->index));
    const int end = int(change.transformPosition(range->index + range->length));
    return Range{start, end - start};
}

/// Quill's transformStack: moves every step past `change`, newest first.
template<class Item>
void transformStack(QList<Item>& stack, Delta change)
{
    for (qsizetype i = stack.size() - 1; i >= 0; --i) {
        const Item old = stack.at(i);
        stack[i].delta = change.transform(old.delta, true);
        stack[i].range = transformRange(old.range, change);
        change = old.delta.transform(change, false);
        if (stack.at(i).delta.isEmpty())
            stack.removeAt(i);
    }
}

} // namespace

History::History(QuillEditor* editor, const HistoryOptions& options)
    : QObject(editor), m_editor(editor), m_options(options)
{
    connect(editor, &QuillEditor::textChange, this, &History::onTextChange);
    connect(editor, &QuillEditor::selectionChange, this, [this](Range range, Range, Source source) {
        if (source != Source::Silent)
            m_currentRange = range;
    });
}

void History::setOptions(const HistoryOptions& options)
{
    m_options = options;
    const bool undo = canUndo(), redo = canRedo();
    while (m_undo.size() > std::max(0, m_options.maxStack))
        m_undo.removeFirst();
    notify(undo, redo);
}

QList<Delta> History::undoStack() const
{
    QList<Delta> out;
    for (const Item& i : m_undo)
        out.append(i.delta);
    return out;
}

QList<Delta> History::redoStack() const
{
    QList<Delta> out;
    for (const Item& i : m_redo)
        out.append(i.delta);
    return out;
}

void History::onTextChange(const Delta& change, const Delta& oldContents, Source source)
{
    const bool undo = canUndo(), redo = canRedo();
    if (!m_ignoreChange) {
        if (!m_options.userOnly || source == Source::User)
            record(change, oldContents);
        else
            transform(change);
    }
    m_currentRange = transformRange(m_currentRange, change);
    notify(undo, redo);
}

void History::record(const Delta& change, const Delta& oldContents)
{
    if (change.isEmpty())
        return;
    m_redo.clear();
    Item item{change.invert(oldContents), m_currentRange};
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRecorded + m_options.delay > now && !m_undo.isEmpty()) {
        const Item last = m_undo.takeLast();
        item.delta = item.delta.compose(last.delta);
        item.range = last.range;
    } else {
        m_lastRecorded = now;
    }
    if (item.delta.isEmpty())
        return;
    m_undo.append(item);
    if (m_undo.size() > std::max(1, m_options.maxStack))
        m_undo.removeFirst();
}

void History::transform(const Delta& change)
{
    transformStack(m_undo, change);
    transformStack(m_redo, change);
}

bool History::undo()
{
    return change(m_undo, m_redo);
}

bool History::redo()
{
    return change(m_redo, m_undo);
}

bool History::change(QList<Item>& from, QList<Item>& to)
{
    if (from.isEmpty() || m_editor->isReadOnly())
        return false;
    const bool undo = canUndo(), redo = canRedo();
    const Item item = from.takeLast();
    const Delta base = m_editor->getContents();
    m_lastRecorded = 0;
    m_ignoreChange = true;
    const Delta applied = m_editor->updateContents(item.delta, Source::User);
    m_ignoreChange = false;
    // The inverse of what was applied (the editor may normalise a step that
    // no longer fits, e.g. formats on a line that became an embed).
    const Delta inverse = applied.invert(base);
    if (!inverse.isEmpty())
        to.append({inverse, transformRange(item.range, inverse)});

    const int last = m_editor->getLength() - 1;
    if (m_editor->editingRules() == QuillEditor::EditingRules::QuillJs && item.range) {
        m_editor->setSelection(std::min(item.range->index, last), item.range->length, Source::User);
    } else if (m_editor->editingRules() == QuillEditor::EditingRules::QuillJs) {
        m_editor->setSelection(std::min(lastChangeIndex(item.delta), last), 0, Source::User);
    } else {
        // flutter_quill: after the inserts and retains of the step.
        int len = 0;
        for (const Op& op : item.delta.ops()) {
            if (!op.isDelete())
                len += int(op.length());
        }
        m_editor->setSelection(std::min(len, last), 0, Source::User);
    }
    notify(undo, redo);
    return true;
}

void History::clear()
{
    const bool undo = canUndo(), redo = canRedo();
    m_undo.clear();
    m_redo.clear();
    m_lastRecorded = 0;
    notify(undo, redo);
}

int History::lastChangeIndex(const Delta& change)
{
    qsizetype deleted = 0;
    for (const Op& op : change.ops()) {
        if (op.isDelete())
            deleted += op.length();
    }
    qsizetype index = change.length() - deleted;
    if (!change.ops().isEmpty()) {
        const Op& last = change.ops().constLast();
        bool newline = false;
        if (last.isInsert()) {
            newline = last.isTextInsert() && last.text().endsWith(QLatin1Char('\n'));
        } else if (last.hasAttributes()) {
            const auto& lineKeys = formats::lineKeys();
            const AttributeMap attrs = last.attributes();
            for (auto it = attrs.begin(); it != attrs.end() && !newline; ++it)
                newline = lineKeys.contains(it.key());
        }
        if (newline)
            --index;
    }
    return int(index);
}

void History::notify(bool undoBefore, bool redoBefore)
{
    if (canUndo() != undoBefore)
        Q_EMIT undoAvailable(canUndo());
    if (canRedo() != redoBefore)
        Q_EMIT redoAvailable(canRedo());
}

} // namespace quill
