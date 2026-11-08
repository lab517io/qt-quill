#pragma once

// Helpers for QuillEditor tests.

#include "TestUtil.h"

#include <quill/DeltaDocument.h>
#include <quill/QuillEditor.h>

#include <QObject>
#include <QTest>

#include <memory>

namespace testutil {

struct ChangeRecord
{
    quill::Delta change;
    quill::Delta old;
    quill::Source source;
};

/// Records textChange signals and checks each one: old.compose(change) must
/// be the editor's contents, which must equal a full export of the document.
class ChangeRecorder
{
public:
    explicit ChangeRecorder(quill::QuillEditor* editor) : m_editor(editor)
    {
        QObject::connect(editor, &quill::QuillEditor::textChange, &m_context,
                         [this](const quill::Delta& change, const quill::Delta& old, quill::Source source) {
                             records.append({change, old, source});
                             const quill::Delta now = m_editor->getContents();
                             if (old.compose(change) != now) {
                                 ++mismatches;
                                 qWarning("change does not compose:\n  old: %s\n  change: %s\n  now: %s",
                                          old.toJson().constData(), change.toJson().constData(),
                                          now.toJson().constData());
                             }
                             const quill::Delta exported = quill::DeltaDocument::toDelta(m_editor->document());
                             if (exported != now) {
                                 ++mismatches;
                                 qWarning("contents differ from export:\n  contents: %s\n  export: %s",
                                          now.toJson().constData(), exported.toJson().constData());
                             }
                         });
    }

    const ChangeRecord& last() const { return records.constLast(); }
    void clear() { records.clear(); }

    QList<ChangeRecord> records;
    int mismatches = 0;

private:
    quill::QuillEditor* m_editor;
    QObject m_context;
};

inline std::unique_ptr<quill::QuillEditor> makeEditor(bool show = true)
{
    auto e = std::make_unique<quill::QuillEditor>();
    e->setVerifyChanges(true);
    e->resize(600, 400);
    if (show) {
        e->show();
        if (!QTest::qWaitForWindowExposed(e.get()))
            qWarning("editor window not exposed");
    }
    return e;
}

} // namespace testutil

namespace quill {
inline char* toString(const Range& r)
{
    return QTest::toString(QStringLiteral("Range{%1, %2}").arg(r.index).arg(r.length));
}
} // namespace quill
