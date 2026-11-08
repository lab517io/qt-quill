#pragma once

#include "quill/Attributes.h"

#include <QColor>
#include <QHash>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QToolBar>

#include <optional>

class QComboBox;

namespace quill {

class QuillEditor;
class QuillFindBar;

namespace detail {
class ColorButton;
}

/// Formatting toolbar for a QuillEditor with the controls of flutter_quill's
/// default toolbar.
///
/// Every control is reachable by name through action(), so it can be placed
/// in menus or hidden. Names and groups:
///
/// | group   | actions |
/// |---------|---------|
/// | history | undo, redo |
/// | font    | font, size (combo boxes) |
/// | inline  | bold, italic, underline, strike, code, subscript, superscript, small (hidden by default, as in flutter_quill) |
/// | color   | color, background |
/// | clear   | clear |
/// | header  | header (combo box) |
/// | list    | list-bullet, list-ordered, list-check, outdent, indent |
/// | align   | align-left, align-center, align-right, align-justify, direction-rtl |
/// | block   | blockquote, code-block |
/// | insert  | link, image, video, formula |
/// | search  | search (only when a find bar is set) |
///
/// Dialogs are virtual methods so applications can substitute their own.
class QuillToolbar : public QToolBar
{
    Q_OBJECT
public:
    /// A choice of a combo box: label and attribute value (null clears).
    using Choice = QPair<QString, QJsonValue>;

    struct LinkEdit
    {
        QString text;
        QString url;
        bool remove = false;
    };

    explicit QuillToolbar(QWidget* parent = nullptr);
    explicit QuillToolbar(QuillEditor* editor, QWidget* parent = nullptr);
    ~QuillToolbar() override;

    void setEditor(QuillEditor* editor);
    QuillEditor* editor() const;
    /// The find bar the "search" action toggles.
    void setFindBar(QuillFindBar* bar);

    /// The action for a control name (see the table), or nullptr.
    QAction* action(const QString& name) const;
    QStringList actionNames() const;
    static QStringList groupNames();
    QStringList groupActions(const QString& group) const;
    void setGroupVisible(const QString& group, bool visible);

    /// flutter_quill defaults: Sans Serif, Serif, Monospace, Ibarra Real Nova,
    /// SquarePeg, Nunito, Pacifico, Roboto Mono, Clear.
    void setFontFamilies(const QList<Choice>& choices);
    /// Default: Small, Large, Huge, 10 ... 48, Clear.
    void setFontSizes(const QList<Choice>& choices);
    void setPaletteColors(const QList<QColor>& colors);

public Q_SLOTS:
    /// Refreshes checked states and combo boxes from the editor.
    void updateState();

protected:
    virtual std::optional<LinkEdit> promptLink(const LinkEdit& current);
    /// An image URL (file dialog: file URL); empty cancels.
    virtual QString promptImage();
    virtual QString promptVideo();
    virtual QString promptFormula();
    virtual std::optional<QColor> promptColor(const QColor& initial, const QString& title);

private:
    void build();
    QAction* addToggle(const QString& group, const QString& name, const QString& text);
    QAction* addButton(const QString& group, const QString& name, const QString& text);
    QComboBox* addCombo(const QString& group, const QString& name, const QString& tip);
    void fillCombo(QComboBox* combo, const QList<Choice>& choices);
    void comboActivated(QComboBox* combo, const QString& attribute, int index);
    void toggle(const QString& name, bool checked);
    void applyColor(const QString& attribute, const std::optional<QColor>& color);
    void clearFormat();
    void editLink();
    void insertEmbed(const QString& type, const QString& value);
    void syncCombo(QComboBox* combo, const QJsonValue& value);
    void refocus();

    QPointer<QuillEditor> m_editor;
    QPointer<QuillFindBar> m_findBar;
    QHash<QString, QAction*> m_actions;
    QHash<QString, QStringList> m_groups;
    QStringList m_order;
    QComboBox* m_font = nullptr;
    QComboBox* m_size = nullptr;
    QComboBox* m_header = nullptr;
    detail::ColorButton* m_color = nullptr;
    detail::ColorButton* m_background = nullptr;
    QList<QMetaObject::Connection> m_editorConnections;
    bool m_updating = false;
};

} // namespace quill
