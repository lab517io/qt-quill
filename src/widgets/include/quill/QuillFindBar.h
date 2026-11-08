#pragma once

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;

namespace quill {

class QuillEditor;

/// Search bar for a QuillEditor: highlights all matches (as extra
/// selections, so the document is not changed) and steps through them.
/// Enter: next, Shift+Enter: previous, Escape: close.
class QuillFindBar : public QWidget
{
    Q_OBJECT
public:
    explicit QuillFindBar(QWidget* parent = nullptr);

    void setEditor(QuillEditor* editor);
    QuillEditor* editor() const { return m_editor; }

    QLineEdit* lineEdit() const { return m_input; }
    void setCaseSensitive(bool on);
    int matchCount() const { return int(m_matches.size()); }
    /// 1-based index of the current match, 0 when none is selected.
    int currentMatch() const { return m_current + 1; }

public Q_SLOTS:
    /// Shows the bar, focuses the input and selects its text.
    void activate();
    void findNext();
    void findPrevious();
    /// Hides the bar and removes the highlights.
    void close();

Q_SIGNALS:
    void closed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void step(int direction);
    void highlight();

    QPointer<QuillEditor> m_editor;
    QList<QMetaObject::Connection> m_connections;
    QLineEdit* m_input;
    QCheckBox* m_case;
    QLabel* m_count;
    QList<std::pair<int, int>> m_matches; // position, length
    int m_current = -1;
};

} // namespace quill
