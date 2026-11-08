#pragma once

// Dialog for inserting or editing a link (private header).

#include <QDialog>

class QLineEdit;

namespace quill::detail {

class LinkDialog : public QDialog
{
    Q_OBJECT
public:
    /// `editing`: the selection is an existing link; offers "Remove".
    LinkDialog(const QString& text, const QString& url, bool editing, QWidget* parent = nullptr);

    QString text() const;
    QString url() const;
    bool removeRequested() const { return m_remove; }

private:
    QLineEdit* m_text;
    QLineEdit* m_url;
    bool m_remove = false;
};

} // namespace quill::detail
