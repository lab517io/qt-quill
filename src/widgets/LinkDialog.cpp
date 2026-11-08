#include "LinkDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

namespace quill::detail {

LinkDialog::LinkDialog(const QString& text, const QString& url, bool editing, QWidget* parent)
    : QDialog(parent), m_text(new QLineEdit(text, this)), m_url(new QLineEdit(url, this))
{
    setWindowTitle(editing ? tr("Edit link") : tr("Insert link"));
    m_url->setPlaceholderText(QStringLiteral("https://"));
    auto* form = new QFormLayout(this);
    form->addRow(tr("Text"), m_text);
    form->addRow(tr("URL"), m_url);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (editing) {
        QPushButton* remove = buttons->addButton(tr("Remove link"), QDialogButtonBox::DestructiveRole);
        connect(remove, &QPushButton::clicked, this, [this] {
            m_remove = true;
            accept();
        });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    const auto updateOk = [this, buttons] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_url->text().trimmed().isEmpty());
    };
    connect(m_url, &QLineEdit::textChanged, this, updateOk);
    updateOk();
    form->addRow(buttons);
    (url.isEmpty() ? m_url : m_text)->setFocus();
    resize(420, sizeHint().height());
}

QString LinkDialog::text() const { return m_text->text(); }
QString LinkDialog::url() const { return m_url->text().trimmed(); }

} // namespace quill::detail
