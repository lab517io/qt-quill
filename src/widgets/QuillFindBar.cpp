#include "quill/QuillFindBar.h"

#include "Icons.h"
#include "quill/QuillEditor.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>

namespace quill {

QuillFindBar::QuillFindBar(QWidget* parent)
    : QWidget(parent), m_input(new QLineEdit(this)), m_case(new QCheckBox(tr("Match case"), this)),
      m_count(new QLabel(this))
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    m_input->setPlaceholderText(tr("Find"));
    m_input->setClearButtonEnabled(true);
    m_input->installEventFilter(this);
    const auto button = [this](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(detail::toolbarIcon(icon));
        b->setToolTip(tip);
        b->setAutoRaise(true);
        return b;
    };
    QToolButton* prev = button(QStringLiteral("previous"), tr("Previous match (Shift+Enter)"));
    QToolButton* next = button(QStringLiteral("next"), tr("Next match (Enter)"));
    QToolButton* close = button(QStringLiteral("close"), tr("Close (Escape)"));
    layout->addWidget(m_input, 1);
    layout->addWidget(m_count);
    layout->addWidget(prev);
    layout->addWidget(next);
    layout->addWidget(m_case);
    layout->addWidget(close);
    connect(m_input, &QLineEdit::textChanged, this, &QuillFindBar::refresh);
    connect(m_case, &QCheckBox::toggled, this, &QuillFindBar::refresh);
    connect(prev, &QToolButton::clicked, this, &QuillFindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &QuillFindBar::findNext);
    connect(close, &QToolButton::clicked, this, &QuillFindBar::close);
}

void QuillFindBar::setEditor(QuillEditor* editor)
{
    for (const auto& c : m_connections)
        disconnect(c);
    m_connections.clear();
    m_editor = editor;
    if (editor) {
        m_connections << connect(editor, &QuillEditor::textChange, this, [this] {
            if (isVisible())
                refresh();
        });
        m_connections << connect(editor, &QObject::destroyed, this, [this] {
            for (const auto& c : m_connections)
                disconnect(c);
            m_connections.clear();
            m_editor = nullptr;
            m_matches.clear();
        });
    }
    refresh();
}

void QuillFindBar::setCaseSensitive(bool on) { m_case->setChecked(on); }

void QuillFindBar::activate()
{
    show();
    if (m_editor && m_editor->textCursor().hasSelection() && !m_editor->textCursor().selectedText().contains(QChar::ParagraphSeparator))
        m_input->setText(m_editor->textCursor().selectedText());
    m_input->setFocus();
    m_input->selectAll();
    refresh();
}

void QuillFindBar::close()
{
    hide();
    if (m_editor) {
        m_editor->setExtraSelections({});
        m_editor->setFocus();
    }
    Q_EMIT closed();
}

void QuillFindBar::findNext() { step(1); }
void QuillFindBar::findPrevious() { step(-1); }

bool QuillFindBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(event);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            step(k->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
        if (k->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void QuillFindBar::refresh()
{
    m_matches.clear();
    m_current = -1;
    const QString needle = m_input->text();
    if (m_editor && !needle.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (m_case->isChecked())
            flags |= QTextDocument::FindCaseSensitively;
        QTextCursor c(m_editor->document());
        while (true) {
            c = m_editor->document()->find(needle, c, flags);
            if (c.isNull())
                break;
            m_matches.append({c.selectionStart(), c.selectionEnd() - c.selectionStart()});
        }
        // The first match at or after the cursor becomes current.
        const int from = m_editor->textCursor().selectionStart();
        for (int i = 0; i < m_matches.size(); ++i) {
            if (m_matches.at(i).first >= from) {
                m_current = i;
                break;
            }
        }
        if (m_current < 0 && !m_matches.isEmpty())
            m_current = 0;
    }
    highlight();
}

void QuillFindBar::step(int direction)
{
    if (m_matches.isEmpty() || !m_editor)
        return;
    const int n = int(m_matches.size());
    // Step from the match that is selected, if any.
    const QTextCursor sel = m_editor->textCursor();
    const bool onCurrent = m_current >= 0 && sel.selectionStart() == m_matches.at(m_current).first
                           && sel.selectionEnd() == m_matches.at(m_current).first + m_matches.at(m_current).second;
    if (onCurrent || m_current < 0)
        m_current = ((m_current < 0 ? (direction > 0 ? -1 : 0) : m_current) + direction + n) % n;
    const auto [pos, len] = m_matches.at(m_current);
    m_editor->setSelection(pos, len, Source::User);
    m_editor->ensureCursorVisible();
    highlight();
}

void QuillFindBar::highlight()
{
    m_count->setText(m_input->text().isEmpty() ? QString()
                     : m_matches.isEmpty()     ? tr("No results")
                                               : tr("%1 of %2").arg(m_current + 1).arg(m_matches.size()));
    if (!m_editor)
        return;
    QList<QTextEdit::ExtraSelection> extras;
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(m_editor->document());
        s.cursor.setPosition(m_matches.at(i).first);
        s.cursor.setPosition(m_matches.at(i).first + m_matches.at(i).second, QTextCursor::KeepAnchor);
        s.format.setBackground(i == m_current ? QColor(0xFF, 0x98, 0x00, 160) : QColor(0xFF, 0xEB, 0x3B, 140));
        extras.append(s);
    }
    m_editor->setExtraSelections(extras);
}

} // namespace quill
