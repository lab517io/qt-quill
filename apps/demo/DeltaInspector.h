#pragma once

#include <quill/Delta.h>
#include <quill/Source.h>

#include <QTabWidget>

class QListWidget;
class QPlainTextEdit;
class QTimer;

namespace quill {
class QuillEditor;
}

/// Shows the editor's document Delta and the stream of change Deltas.
class DeltaInspector : public QTabWidget
{
    Q_OBJECT
public:
    explicit DeltaInspector(quill::QuillEditor* editor, QWidget* parent = nullptr);

    void clearChanges();

private:
    void refreshDocument();
    void addChange(const quill::Delta& change, const quill::Delta& old, quill::Source source);
    void showChange(int row);

    quill::QuillEditor* m_editor;
    QPlainTextEdit* m_document;
    QListWidget* m_changes;
    QPlainTextEdit* m_details;
    QTimer* m_debounce;
    struct Entry
    {
        quill::Delta change;
        quill::Delta old;
    };
    QList<Entry> m_entries;
};
