#include "DeltaInspector.h"

#include <quill/QuillEditor.h>

#include <QFontDatabase>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>

namespace {
constexpr int kMaxChanges = 500;

// One op per line, keys in Delta order ("insert" first, "attributes" last).
QString pretty(const quill::Delta& d)
{
    QString out = QStringLiteral("[\n");
    for (qsizetype i = 0; i < d.ops().size(); ++i) {
        const QByteArray op = quill::Delta(QList<quill::Op>{d.ops().at(i)}).toJson();
        out += QStringLiteral("  ") + QString::fromUtf8(op.mid(1, op.size() - 2));
        out += i + 1 < d.ops().size() ? QStringLiteral(",\n") : QStringLiteral("\n");
    }
    return out + QStringLiteral("]\n");
}

QPlainTextEdit* jsonView(QWidget* parent)
{
    auto* view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return view;
}
} // namespace

DeltaInspector::DeltaInspector(quill::QuillEditor* editor, QWidget* parent)
    : QTabWidget(parent), m_editor(editor), m_document(jsonView(this)), m_changes(new QListWidget(this)),
      m_details(jsonView(this)), m_debounce(new QTimer(this))
{
    addTab(m_document, tr("Document"));
    auto* split = new QSplitter(Qt::Vertical, this);
    split->addWidget(m_changes);
    split->addWidget(m_details);
    m_changes->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    addTab(split, tr("Changes"));

    m_debounce->setSingleShot(true);
    m_debounce->setInterval(100);
    connect(m_debounce, &QTimer::timeout, this, &DeltaInspector::refreshDocument);
    connect(editor, &quill::QuillEditor::textChange, this,
            [this](const quill::Delta& change, const quill::Delta& old, quill::Source source) {
                addChange(change, old, source);
                m_debounce->start();
            });
    connect(m_changes, &QListWidget::currentRowChanged, this, &DeltaInspector::showChange);
    refreshDocument();
}

void DeltaInspector::clearChanges()
{
    m_entries.clear();
    m_changes->clear();
    m_details->clear();
    refreshDocument();
}

void DeltaInspector::refreshDocument()
{
    const int scroll = m_document->verticalScrollBar() ? m_document->verticalScrollBar()->value() : 0;
    m_document->setPlainText(pretty(m_editor->getContents()));
    m_document->verticalScrollBar()->setValue(scroll);
}

void DeltaInspector::addChange(const quill::Delta& change, const quill::Delta& old, quill::Source source)
{
    static const char* names[] = {"api   ", "user  ", "silent"};
    m_entries.append({change, old});
    m_changes->addItem(QString::fromLatin1(names[int(source)]) + QString::fromUtf8(change.toJson()));
    while (m_entries.size() > kMaxChanges) {
        m_entries.removeFirst();
        delete m_changes->takeItem(0);
    }
    m_changes->scrollToBottom();
}

void DeltaInspector::showChange(int row)
{
    if (row < 0 || row >= m_entries.size()) {
        m_details->clear();
        return;
    }
    m_details->setPlainText(
        tr("change:\n%1\nold contents:\n%2").arg(pretty(m_entries.at(row).change), pretty(m_entries.at(row).old)));
}
