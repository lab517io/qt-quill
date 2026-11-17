#pragma once

#include "quill/Delta.h"
#include "quill/Range.h"
#include "quill/Source.h"

#include <QList>
#include <QObject>

#include <optional>

namespace quill {

class QuillEditor;

struct HistoryOptions
{
    /// Changes within this many milliseconds of the first change of an undo
    /// step join that step (flutter_quill's interval: 400; Quill.js: 1000).
    int delay = 400;
    /// Undo steps kept; the oldest are dropped.
    int maxStack = 100;
    /// Only Source::User changes are recorded; other changes (a remote
    /// peer's, applied with Source::Api) move the recorded steps instead,
    /// so that undo never removes them. Needed for collaborative editing.
    bool userOnly = false;
};

/// Delta-based undo and redo: a port of flutter_quill's History, which is
/// Quill.js's history module.
///
/// Each undo step is the inverse of the change it undoes. Changes that are
/// not recorded are transformed into the steps (Quill's transformStack), so
/// an undo after a remote edit still undoes exactly the local edit, at its
/// new position. Undo and redo apply through updateContents with
/// Source::User, so they emit textChange like any user edit (and reach a
/// collaboration server). The selection after them follows the editor's
/// editingRules(): FlutterQuill collapses it after the change, QuillJs
/// restores the selection the step was recorded with.
///
/// Installed with QuillEditor::installHistory(), which turns off the
/// QTextDocument undo stack and routes the undo keys, the context menu,
/// QuillEditor::undo() / redo() and QuillToolbar through this object.
class History : public QObject
{
    Q_OBJECT
public:
    /// Use QuillEditor::installHistory().
    History(QuillEditor* editor, const HistoryOptions& options);

    HistoryOptions options() const { return m_options; }
    void setOptions(const HistoryOptions& options);

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    /// Undoes the last step; false when there was none (or read-only).
    bool undo();
    bool redo();
    void clear();
    /// The next change starts a new undo step even within the delay.
    void cutoff() { m_lastRecorded = 0; }

    /// The steps, oldest first: each is the change that undoes (redoes) it.
    QList<Delta> undoStack() const;
    QList<Delta> redoStack() const;

    /// Quill's getLastChangeIndex: where the cursor goes after applying `change`.
    static int lastChangeIndex(const Delta& change);

Q_SIGNALS:
    void undoAvailable(bool available);
    void redoAvailable(bool available);

private:
    struct Item
    {
        Delta delta;
        std::optional<Range> range;
    };

    void onTextChange(const Delta& change, const Delta& oldContents, Source source);
    void record(const Delta& change, const Delta& oldContents);
    void transform(const Delta& change);
    bool change(QList<Item>& from, QList<Item>& to);
    void notify(bool undoBefore, bool redoBefore);

    QuillEditor* m_editor;
    HistoryOptions m_options;
    QList<Item> m_undo;
    QList<Item> m_redo;
    qint64 m_lastRecorded = 0;
    bool m_ignoreChange = false;
    std::optional<Range> m_currentRange;
};

} // namespace quill
