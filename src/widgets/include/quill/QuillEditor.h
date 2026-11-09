#pragma once

#include "quill/Delta.h"
#include "quill/Formats.h"
#include "quill/Range.h"
#include "quill/Source.h"

#include <QImage>
#include <QStringList>
#include <QTextEdit>
#include <QUrl>

#include <climits>
#include <memory>

namespace quill {

class EmbedRegistry;
class QuillEditorPrivate;

/// A rich text editor whose content is a Quill Delta.
///
/// The API mirrors Quill's (getContents, updateContents, formatText, ...):
/// indices are Delta indices, attribute names and values are Quill's, and
/// every method that changes the document returns the change Delta.
///
/// Signals carry Quill's change events. They are named textChange and
/// selectionChange (Quill's "text-change" / "selection-change") so that they
/// do not overload QTextEdit::textChanged() and QTextEdit::selectionChanged(),
/// which keep working.
///
/// Indices are UTF-16 code units, as in Quill.js. An index inside a
/// surrogate pair (an emoji) splits it; flutter_quill cannot lay out the
/// resulting lone surrogates, so keep API indices on code point boundaries.
///
/// The document belongs to the editor; do not replace it with setDocument().
/// Changes made to document() with plain QTextCursor calls are still tracked
/// and reported as Source::User changes.
class QuillEditor : public QTextEdit
{
    Q_OBJECT
public:
    /// Whose keyboard rules the editor follows where flutter_quill and
    /// Quill.js differ. FlutterQuill (default): Backspace at a line start
    /// merges the lines (the merged line keeps the upper line's format),
    /// Enter on a checked item continues checked. QuillJs: Backspace at a
    /// line start first outdents and removes list, header, quote and code
    /// formats; a checked item continues unchecked.
    enum class EditingRules { FlutterQuill, QuillJs };
    Q_ENUM(EditingRules)

    explicit QuillEditor(QWidget* parent = nullptr);
    ~QuillEditor() override;

    // --- Content ---------------------------------------------------------------
    /// The document Delta, or the part [index, index + length).
    Delta getContents(int index = 0, int length = INT_MAX) const;
    /// Replaces the document. Resets the undo history (unlike Quill.js).
    Delta setContents(const Delta& document, Source source = Source::Api);
    /// Applies a change Delta. See DeltaDocument::apply for the rules.
    Delta updateContents(const Delta& change, Source source = Source::Api);
    /// Plain text; embeds are U+FFFC so indices stay aligned (flutter_quill's
    /// getPlainText; Quill.js drops embeds).
    QString getText(int index = 0, int length = INT_MAX) const;
    /// Document length including the final "\n" (== document()->characterCount()).
    int getLength() const;

    /// Inserts text with exactly `attributes` (Quill.js would also let the text
    /// inherit the formats of the neighbouring text; pass them instead).
    /// Newlines in `text` split the line and keep its line format.
    Delta insertText(int index, const QString& text, const AttributeMap& attributes = {}, Source source = Source::Api);
    Delta insertEmbed(int index, const QString& type, const QJsonValue& value, Source source = Source::Api);
    /// Inserts an embed on a line of its own, as flutter_quill inserts block
    /// embeds (images, videos, custom): the line is split around it when
    /// `index` is not at a line start or end.
    Delta insertBlockEmbed(int index, const QString& type, const QJsonValue& value, Source source = Source::Api);
    Delta deleteText(int index, int length, Source source = Source::Api);

    // --- Formatting ------------------------------------------------------------
    /// Formats the selection: line formats apply to the selected lines, inline
    /// formats to the selected text, or, when the selection is collapsed, to
    /// the text typed next.
    Delta format(const QString& name, const QJsonValue& value, Source source = Source::Api);
    Delta formatText(int index, int length, const AttributeMap& attributes, Source source = Source::Api);
    /// Applies line formats to every line intersecting [index, index + max(length, 1)).
    /// "indent": "+1" / "-1" change each line's indent by one (0..8); setting
    /// "list", "blockquote" or "code-block" removes the other two.
    Delta formatLine(int index, int length, const AttributeMap& attributes, Source source = Source::Api);
    /// Removes all formats from the text and lines in the range (embeds keep
    /// their attributes, Quill.js would delete them).
    Delta removeFormat(int index, int length, Source source = Source::Api);
    /// Formats shared by the whole range; a format with different values
    /// becomes an array of the values (Quill's combineFormats). A collapsed
    /// range reports the character before it (flutter_quill).
    AttributeMap getFormat(int index, int length = 0) const;
    /// Formats of the selection, including formats toggled for the text typed next.
    AttributeMap getFormat() const;

    // --- Selection -------------------------------------------------------------
    Range getSelection() const;
    void setSelection(int index, int length = 0, Source source = Source::Api);

    // --- Embeds ----------------------------------------------------------------
    /// The handlers drawing this editor's embeds. Register handlers for own
    /// embed keys or custom types here; remote images are enabled.
    EmbedRegistry* embeds() const;
    /// Index of the embed at viewport position `pos`, or -1.
    int embedAt(const QPoint& pos) const;

    // --- Options ---------------------------------------------------------------
    StyleOptions styleOptions() const;
    /// Changes the styling and re-renders the document (contents unchanged).
    void setStyleOptions(const StyleOptions& style);
    EditingRules editingRules() const;
    void setEditingRules(EditingRules rules);
    /// Attributes dropped from pasted HTML. Default: font, size, color, background.
    QStringList pasteIgnoredAttributes() const;
    void setPasteIgnoredAttributes(const QStringList& keys);

    /// Testing aid: after every change, compare the tracked contents with a
    /// full export and count mismatches (then resynchronise). Also enabled by
    /// the environment variable QUILLQT_VERIFY=1.
    void setVerifyChanges(bool on);
    int verificationFailures() const;

Q_SIGNALS:
    void textChange(const quill::Delta& change, const quill::Delta& oldContents, quill::Source source);
    void selectionChange(quill::Range range, quill::Range oldRange, quill::Source source);
    /// After either textChange or selectionChange.
    void editorChange();
    /// Ctrl+click on a link, or a click in read-only mode.
    void linkActivated(const QUrl& url);
    /// Ctrl+K: the host should ask for a URL and call format("link", url).
    void linkRequested();
    /// A click (images: double click) on an embed whose handler did not
    /// handle EmbedHandler::activate(), e.g. to play a video or edit a formula.
    void embedActivated(const QString& type, const QJsonValue& data, int index);

protected:
    /// Source URL for a pasted or dropped image; the default is a PNG data:
    /// URL. Override to upload the image and return its URL.
    virtual QString imageSourceFor(const QImage& image);

    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool viewportEvent(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void changeEvent(QEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;
    QMimeData* createMimeDataFromSelection() const override;

private:
    friend class QuillEditorPrivate;
    std::unique_ptr<QuillEditorPrivate> d;
};

} // namespace quill
