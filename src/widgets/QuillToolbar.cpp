#include "quill/QuillToolbar.h"

#include "DocumentEdit.h"
#include "quill/EmbedRegistry.h"

#include "ColorButton.h"
#include "Icons.h"
#include "LinkDialog.h"
#include "quill/DeltaDocument.h"
#include "quill/QuillEditor.h"
#include "quill/QuillFindBar.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>

namespace quill {

namespace {

QString key(const char* k) { return QString::fromLatin1(k); }
const QJsonValue Null(QJsonValue::Null);

QList<QuillToolbar::Choice> defaultFonts()
{
    // flutter_quill QuillToolbarFontFamilyButton defaults.
    return {{QStringLiteral("Sans Serif"), key("sans-serif")},
            {QStringLiteral("Serif"), key("serif")},
            {QStringLiteral("Monospace"), key("monospace")},
            {QStringLiteral("Ibarra Real Nova"), key("ibarra-real-nova")},
            {QStringLiteral("SquarePeg"), key("square-peg")},
            {QStringLiteral("Nunito"), key("nunito")},
            {QStringLiteral("Pacifico"), key("pacifico")},
            {QStringLiteral("Roboto Mono"), key("roboto-mono")},
            {QObject::tr("Clear"), Null}};
}

QList<QuillToolbar::Choice> defaultSizes()
{
    QList<QuillToolbar::Choice> sizes{{QObject::tr("Small"), key("small")},
                                      {QObject::tr("Large"), key("large")},
                                      {QObject::tr("Huge"), key("huge")}};
    for (int s : {10, 12, 14, 16, 18, 20, 24, 32, 48})
        sizes.append({QString::number(s), QString::number(s)});
    sizes.append({QObject::tr("Clear"), Null});
    return sizes;
}

bool isOn(const QJsonValue& v) { return !v.isUndefined() && !v.isNull() && v != QJsonValue(false) && !v.isArray(); }

} // namespace

QuillToolbar::QuillToolbar(QWidget* parent) : QToolBar(parent) { build(); }

QuillToolbar::QuillToolbar(QuillEditor* editor, QWidget* parent) : QToolBar(parent)
{
    build();
    setEditor(editor);
}

QuillToolbar::~QuillToolbar() = default;

QStringList QuillToolbar::groupNames()
{
    return {key("history"), key("font"), key("inline"), key("color"), key("clear"), key("header"),
            key("list"),    key("align"), key("block"), key("insert"), key("search")};
}

QAction* QuillToolbar::addToggle(const QString& group, const QString& name, const QString& text)
{
    QAction* a = addButton(group, name, text);
    a->setCheckable(true);
    connect(a, &QAction::triggered, this, [this, name](bool checked) { toggle(name, checked); });
    return a;
}

QAction* QuillToolbar::addButton(const QString& group, const QString& name, const QString& text)
{
    QAction* a = addAction(detail::toolbarIcon(name), text);
    a->setObjectName(name);
    m_actions.insert(name, a);
    m_groups[group].append(name);
    m_order.append(name);
    return a;
}

QComboBox* QuillToolbar::addCombo(const QString& group, const QString& name, const QString& tip)
{
    auto* combo = new QComboBox(this);
    combo->setObjectName(name);
    combo->setToolTip(tip);
    combo->setFocusPolicy(Qt::ClickFocus);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    QAction* a = addWidget(combo);
    a->setObjectName(name);
    a->setText(tip);
    m_actions.insert(name, a);
    m_groups[group].append(name);
    m_order.append(name);
    return combo;
}

void QuillToolbar::build()
{
    setObjectName(QStringLiteral("QuillToolbar"));
    setIconSize(QSize(20, 20));

    connect(addButton(key("history"), key("undo"), tr("Undo (Ctrl+Z)")), &QAction::triggered, this, [this] {
        if (m_editor)
            m_editor->undo();
    });
    connect(addButton(key("history"), key("redo"), tr("Redo (Ctrl+Shift+Z)")), &QAction::triggered, this, [this] {
        if (m_editor)
            m_editor->redo();
    });
    addSeparator();

    m_font = addCombo(key("font"), key("font"), tr("Font"));
    m_font->setPlaceholderText(tr("Font"));
    fillCombo(m_font, defaultFonts());
    connect(m_font, &QComboBox::activated, this, [this](int i) { comboActivated(m_font, key("font"), i); });
    m_size = addCombo(key("font"), key("size"), tr("Size"));
    m_size->setPlaceholderText(tr("Size"));
    fillCombo(m_size, defaultSizes());
    connect(m_size, &QComboBox::activated, this, [this](int i) { comboActivated(m_size, key("size"), i); });
    addSeparator();

    addToggle(key("inline"), key("bold"), tr("Bold (Ctrl+B)"));
    addToggle(key("inline"), key("italic"), tr("Italic (Ctrl+I)"));
    addToggle(key("inline"), key("underline"), tr("Underline (Ctrl+U)"));
    addToggle(key("inline"), key("strike"), tr("Strikethrough (Ctrl+Shift+X)"));
    addToggle(key("inline"), key("code"), tr("Inline code"));
    addToggle(key("inline"), key("subscript"), tr("Subscript"));
    addToggle(key("inline"), key("superscript"), tr("Superscript"));
    addToggle(key("inline"), key("small"), tr("Small"))->setVisible(false);
    addSeparator();

    const auto addColor = [this](const QString& name, const QString& tip) {
        auto* b = new detail::ColorButton(name, this);
        b->setObjectName(name);
        b->setToolTip(tip);
        QAction* a = addWidget(b);
        a->setObjectName(name);
        a->setText(tip);
        m_actions.insert(name, a);
        m_groups[key("color")].append(name);
        m_order.append(name);
        connect(b, &detail::ColorButton::colorSelected, this,
                [this, name](const std::optional<QColor>& c) { applyColor(name, c); });
        connect(b, &detail::ColorButton::moreRequested, this, [this, name, b, tip] {
            if (auto c = promptColor(b->currentColor().value_or(Qt::black), tip))
                applyColor(name, c);
        });
        return b;
    };
    m_color = addColor(key("color"), tr("Text colour"));
    m_background = addColor(key("background"), tr("Background colour"));
    connect(addButton(key("clear"), key("clear"), tr("Clear formatting")), &QAction::triggered, this,
            &QuillToolbar::clearFormat);
    addSeparator();

    m_header = addCombo(key("header"), key("header"), tr("Heading"));
    QList<Choice> headers{{tr("Normal"), Null}};
    for (int i = 1; i <= 6; ++i)
        headers.append({tr("Heading %1").arg(i), i});
    fillCombo(m_header, headers);
    connect(m_header, &QComboBox::activated, this, [this](int i) { comboActivated(m_header, key("header"), i); });
    addSeparator();

    addToggle(key("list"), key("list-bullet"), tr("Bullet list"));
    addToggle(key("list"), key("list-ordered"), tr("Numbered list"));
    addToggle(key("list"), key("list-check"), tr("Checklist"));
    connect(addButton(key("list"), key("outdent"), tr("Decrease indent (Shift+Tab)")), &QAction::triggered, this,
            [this] { toggle(key("outdent"), true); });
    connect(addButton(key("list"), key("indent"), tr("Increase indent (Tab)")), &QAction::triggered, this,
            [this] { toggle(key("indent"), true); });
    addSeparator();

    addToggle(key("align"), key("align-left"), tr("Align left"));
    addToggle(key("align"), key("align-center"), tr("Align centre"));
    addToggle(key("align"), key("align-right"), tr("Align right"));
    addToggle(key("align"), key("align-justify"), tr("Justify"));
    addToggle(key("align"), key("direction-rtl"), tr("Right to left"));
    addSeparator();

    addToggle(key("block"), key("blockquote"), tr("Quote"));
    addToggle(key("block"), key("code-block"), tr("Code block"));
    addSeparator();

    QAction* link = addToggle(key("insert"), key("link"), tr("Link (Ctrl+K)"));
    disconnect(link, &QAction::triggered, this, nullptr);
    connect(link, &QAction::triggered, this, [this] { editLink(); });
    connect(addButton(key("insert"), key("image"), tr("Insert image")), &QAction::triggered, this, [this] {
        const QString src = promptImage();
        if (!src.isEmpty())
            insertEmbed(key("image"), src);
    });
    connect(addButton(key("insert"), key("video"), tr("Insert video")), &QAction::triggered, this, [this] {
        const QString url = promptVideo();
        if (!url.isEmpty())
            insertEmbed(key("video"), url);
    });
    connect(addButton(key("insert"), key("formula"), tr("Insert formula")), &QAction::triggered, this, [this] {
        const QString tex = promptFormula();
        if (!tex.isEmpty())
            insertEmbed(key("formula"), tex);
    });
    addSeparator();

    QAction* search = addButton(key("search"), key("search"), tr("Find (Ctrl+F)"));
    search->setCheckable(true);
    search->setVisible(false);
    connect(search, &QAction::triggered, this, [this](bool on) {
        if (!m_findBar)
            return;
        if (on)
            m_findBar->activate();
        else
            m_findBar->close();
    });

    updateState();
}

void QuillToolbar::fillCombo(QComboBox* combo, const QList<Choice>& choices)
{
    combo->clear();
    for (const Choice& c : choices)
        combo->addItem(c.first, c.second.toVariant());
}

void QuillToolbar::setFontFamilies(const QList<Choice>& choices)
{
    fillCombo(m_font, choices);
    updateState();
}

void QuillToolbar::setFontSizes(const QList<Choice>& choices)
{
    fillCombo(m_size, choices);
    updateState();
}

void QuillToolbar::setPaletteColors(const QList<QColor>& colors)
{
    m_color->setColors(colors);
    m_background->setColors(colors);
}

void QuillToolbar::setEditor(QuillEditor* editor)
{
    for (const auto& c : m_editorConnections)
        disconnect(c);
    m_editorConnections.clear();
    m_editor = editor;
    if (editor) {
        const auto refresh = [this] { updateState(); };
        m_editorConnections << connect(editor, &QuillEditor::editorChange, this, refresh)
                            << connect(editor, &QTextEdit::currentCharFormatChanged, this, refresh)
                            << connect(editor, &QTextEdit::cursorPositionChanged, this, refresh)
                            << connect(editor->document(), &QTextDocument::undoAvailable, this, refresh)
                            << connect(editor->document(), &QTextDocument::redoAvailable, this, refresh)
                            << connect(editor, &QuillEditor::linkRequested, this, [this] { editLink(); })
                            << connect(editor, &QObject::destroyed, this, [this] {
                                   // While destroyed() is emitted the QPointer is still set and
                                   // the object is no longer a QuillEditor: do not touch it.
                                   for (const auto& c : m_editorConnections)
                                       disconnect(c);
                                   m_editorConnections.clear();
                                   m_editor = nullptr;
                                   updateState();
                               });
    }
    if (m_findBar)
        m_findBar->setEditor(editor);
    updateState();
}

QuillEditor* QuillToolbar::editor() const { return m_editor; }

void QuillToolbar::setFindBar(QuillFindBar* bar)
{
    m_findBar = bar;
    QAction* search = m_actions.value(key("search"));
    search->setVisible(bar != nullptr);
    if (bar) {
        bar->setEditor(m_editor);
        connect(bar, &QuillFindBar::closed, search, [search] { search->setChecked(false); });
    }
}

QAction* QuillToolbar::action(const QString& name) const { return m_actions.value(name); }
QStringList QuillToolbar::actionNames() const { return m_order; }
QStringList QuillToolbar::groupActions(const QString& group) const { return m_groups.value(group); }

void QuillToolbar::setGroupVisible(const QString& group, bool visible)
{
    for (const QString& name : m_groups.value(group)) {
        if (name == QLatin1String("search") && !m_findBar)
            continue;
        m_actions.value(name)->setVisible(visible);
    }
}

void QuillToolbar::refocus()
{
    if (m_editor)
        m_editor->setFocus();
}

void QuillToolbar::toggle(const QString& name, bool checked)
{
    if (!m_editor || m_updating)
        return;
    QuillEditor* e = m_editor;
    const QJsonValue off = Null;
    static const QStringList inlineToggles{key("bold"), key("italic"), key("underline"), key("strike"), key("code"),
                                           key("small")};
    if (inlineToggles.contains(name)) {
        e->format(name, checked ? QJsonValue(true) : off, Source::User);
    } else if (name == QLatin1String("subscript") || name == QLatin1String("superscript")) {
        e->format(key("script"), checked ? QJsonValue(name == QLatin1String("subscript") ? key("sub") : key("super")) : off,
                  Source::User);
    } else if (name.startsWith(QLatin1String("list-"))) {
        static const QHash<QString, QString> values{
            {key("list-bullet"), key("bullet")}, {key("list-ordered"), key("ordered")}, {key("list-check"), key("unchecked")}};
        e->format(key("list"), checked ? QJsonValue(values.value(name)) : off, Source::User);
    } else if (name == QLatin1String("indent") || name == QLatin1String("outdent")) {
        e->format(key("indent"), name == QLatin1String("indent") ? key("+1") : key("-1"), Source::User);
    } else if (name.startsWith(QLatin1String("align-"))) {
        const QString value = name.mid(6);
        e->format(key("align"), value == QLatin1String("left") || !checked ? off : QJsonValue(value), Source::User);
    } else if (name == QLatin1String("direction-rtl")) {
        e->format(key("direction"), checked ? QJsonValue(key("rtl")) : off, Source::User);
    } else if (name == QLatin1String("blockquote") || name == QLatin1String("code-block")) {
        e->format(name, checked ? QJsonValue(true) : off, Source::User);
    }
    updateState();
    refocus();
}

void QuillToolbar::comboActivated(QComboBox* combo, const QString& attribute, int index)
{
    if (!m_editor || m_updating || index < 0)
        return;
    const QJsonValue value = QJsonValue::fromVariant(combo->itemData(index));
    m_editor->format(attribute, value.isUndefined() ? Null : value, Source::User);
    updateState();
    refocus();
}

void QuillToolbar::applyColor(const QString& attribute, const std::optional<QColor>& color)
{
    if (!m_editor)
        return;
    const StyleOptions style = DeltaDocument::documentStyle(m_editor->document());
    m_editor->format(attribute, color ? QJsonValue(colors::format(*color, QString(), style.colorDialect)) : Null,
                     Source::User);
    updateState();
    refocus();
}

void QuillToolbar::clearFormat()
{
    if (!m_editor)
        return;
    const Range r = m_editor->getSelection();
    if (r.length > 0) {
        m_editor->removeFormat(r.index, r.length, Source::User);
    } else {
        // Collapsed: clear the formats of the text typed next.
        const AttributeMap f = m_editor->getFormat();
        for (auto it = f.begin(); it != f.end(); ++it)
            if (formats::inlineKeys().contains(it.key()))
                m_editor->format(it.key(), Null, Source::User);
    }
    updateState();
    refocus();
}

namespace {

/// The run of characters around `index` that has the link `url`.
Range linkRange(QuillEditor* e, int index, const QString& url)
{
    const Delta doc = e->getContents();
    int pos = 0;
    int start = -1;
    for (const Op& op : doc.ops()) {
        const int len = int(op.length());
        const bool linked = op.attributes().value(key("link")).toString() == url;
        if (linked && start < 0)
            start = pos;
        if (!linked && start >= 0) {
            if (index >= start && index <= pos)
                return {start, pos - start};
            start = -1;
        }
        pos += len;
    }
    if (start >= 0 && index >= start)
        return {start, pos - start};
    return {index, 0};
}

AttributeMap inlineOnly(const AttributeMap& attrs)
{
    AttributeMap out;
    for (auto it = attrs.begin(); it != attrs.end(); ++it)
        if (!formats::lineKeys().contains(it.key()) && !it.value().isArray())
            out.insert(it.key(), it.value());
    return out;
}

} // namespace

void QuillToolbar::editLink()
{
    if (!m_editor || m_editor->isReadOnly())
        return;
    QuillEditor* e = m_editor;
    Range r = e->getSelection();
    const QString current = e->getFormat().value(key("link")).toString();
    if (!current.isEmpty() && r.length == 0)
        r = linkRange(e, r.index, current);
    LinkEdit edit;
    edit.text = e->getText(r.index, r.length);
    edit.url = current;
    const std::optional<LinkEdit> result = promptLink(edit);
    updateState();
    if (!result)
        return refocus();
    if (result->remove) {
        if (r.length > 0)
            e->formatText(r.index, r.length, {{key("link"), Null}}, Source::User);
        return refocus();
    }
    if (result->url.isEmpty())
        return refocus();
    const QString text = result->text.isEmpty() ? result->url : result->text;
    if (r.length > 0 && text == edit.text) {
        e->formatText(r.index, r.length, {{key("link"), result->url}}, Source::User);
    } else {
        AttributeMap attrs = inlineOnly(r.length > 0 ? e->getFormat(r.index, 1) : e->getFormat());
        attrs.insert(key("link"), result->url);
        e->updateContents(Delta().retain(r.index).remove(r.length).insert(text, attrs), Source::User);
        e->setSelection(r.index + int(text.size()), 0, Source::Silent);
    }
    updateState();
    refocus();
}

void QuillToolbar::insertEmbed(const QString& type, const QString& value)
{
    if (!m_editor || m_editor->isReadOnly())
        return;
    const Range r = m_editor->getSelection();
    // Block embeds (image, video) go on a line of their own, as in flutter_quill.
    const bool block = m_editor->embeds()->rendererFor(type, value)->isBlock();
    int at = r.index;
    const Delta change = detail::embedInsertChange(m_editor->getContents(), r.index, r.length,
                                                   QJsonObject{{type, value}}, block, &at);
    m_editor->updateContents(change, Source::User);
    m_editor->setSelection(at + 1, 0, Source::Silent);
    refocus();
}

void QuillToolbar::syncCombo(QComboBox* combo, const QJsonValue& value)
{
    int index = -1;
    if (value.isUndefined() || value.isNull()) {
        // "Normal" for headers; nothing selected for font and size.
        index = combo == m_header ? 0 : -1;
    } else if (!value.isArray()) {
        for (int i = 0; i < combo->count(); ++i) {
            const QJsonValue item = QJsonValue::fromVariant(combo->itemData(i));
            if (item == value || (!item.isNull() && item.toVariant().toString() == value.toVariant().toString())) {
                index = i;
                break;
            }
        }
        if (index < 0 && combo == m_size) { // a size not in the list, e.g. "15.0"
            combo->addItem(value.toVariant().toString(), value.toVariant());
            index = combo->count() - 1;
        }
    }
    combo->setCurrentIndex(index);
}

void QuillToolbar::updateState()
{
    m_updating = true;
    const bool hasEditor = m_editor != nullptr;
    const bool editable = hasEditor && !m_editor->isReadOnly();
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
        it.value()->setEnabled(editable || it.key() == QLatin1String("search"));
    m_actions.value(key("search"))->setEnabled(hasEditor);
    if (!hasEditor) {
        m_updating = false;
        return;
    }
    const AttributeMap f = m_editor->getFormat();
    const auto check = [&](const char* name, bool on) { m_actions.value(key(name))->setChecked(on); };
    for (const char* name : {"bold", "italic", "underline", "strike", "code", "small", "blockquote", "code-block"})
        check(name, isOn(f.value(key(name))));
    const QString script = f.value(key("script")).toString();
    check("subscript", script == QLatin1String("sub"));
    check("superscript", script == QLatin1String("super"));
    const QString list = f.value(key("list")).toString();
    check("list-bullet", list == QLatin1String("bullet"));
    check("list-ordered", list == QLatin1String("ordered"));
    check("list-check", list == QLatin1String("checked") || list == QLatin1String("unchecked"));
    const QJsonValue align = f.value(key("align"));
    check("align-left", align.isUndefined() || align.toString() == QLatin1String("left"));
    check("align-center", align.toString() == QLatin1String("center"));
    check("align-right", align.toString() == QLatin1String("right"));
    check("align-justify", align.toString() == QLatin1String("justify"));
    check("direction-rtl", f.value(key("direction")).toString() == QLatin1String("rtl"));
    check("link", f.value(key("link")).isString());
    syncCombo(m_font, f.value(key("font")));
    syncCombo(m_size, f.value(key("size")));
    syncCombo(m_header, f.value(key("header")));
    const ColorDialect dialect = DeltaDocument::documentStyle(m_editor->document()).colorDialect;
    m_color->setCurrentColor(colors::parse(f.value(key("color")).toString(), dialect));
    m_background->setCurrentColor(colors::parse(f.value(key("background")).toString(), dialect));
    m_actions.value(key("undo"))->setEnabled(editable && m_editor->document()->isUndoAvailable());
    m_actions.value(key("redo"))->setEnabled(editable && m_editor->document()->isRedoAvailable());
    if (m_findBar)
        m_actions.value(key("search"))->setChecked(m_findBar->isVisible());
    m_updating = false;
}

// --- Dialogs -----------------------------------------------------------------

std::optional<QuillToolbar::LinkEdit> QuillToolbar::promptLink(const LinkEdit& current)
{
    detail::LinkDialog dialog(current.text, current.url, !current.url.isEmpty(), this);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return LinkEdit{dialog.text(), dialog.url(), dialog.removeRequested()};
}

QString QuillToolbar::promptImage()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Insert image"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)"));
    return file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
}

QString QuillToolbar::promptVideo()
{
    return QInputDialog::getText(this, tr("Insert video"), tr("Video URL")).trimmed();
}

QString QuillToolbar::promptFormula()
{
    return QInputDialog::getText(this, tr("Insert formula"), tr("TeX")).trimmed();
}

std::optional<QColor> QuillToolbar::promptColor(const QColor& initial, const QString& title)
{
    const QColor c = QColorDialog::getColor(initial, this, title);
    return c.isValid() ? std::optional<QColor>(c) : std::nullopt;
}

} // namespace quill
