#pragma once

#include "quill/EmbedRegistry.h"
#include "quill/QuillEditor.h"

#include <QList>
#include <QPoint>
#include <QRectF>

#include <functional>

class QKeyEvent;
class QMenu;
class QMimeData;
class QMouseEvent;
class QPainter;
class QTextBlock;

namespace quill {

class QuillEditorPrivate
{
public:
    explicit QuillEditorPrivate(QuillEditor* editor) : q(editor) {}
    /// White-box tests.
    static QuillEditorPrivate* get(QuillEditor* editor) { return editor->d.get(); }

    struct Line
    {
        int start = 0; ///< position of the first character
        int end = 0;   ///< position of the line's "\n"
        AttributeMap attrs;
        QString text;
        bool isEmpty() const { return start == end; }
    };

    /// Runs an API operation: tracks the changes it causes and emits one
    /// textChange with `source` when the outermost operation ends. Returns
    /// `fn`'s change when composing it onto the old contents gives the new
    /// contents (what Quill does), otherwise the tracked change.
    Delta modify(Source source, const std::function<Delta()>& fn);
    /// Applies a change Delta to the document (DeltaDocument::apply).
    void apply(const Delta& change);
    void onContentsChange(int pos, int removed, int added);
    /// Diffs the whole document against `contents` (changes whose range Qt
    /// does not report reliably).
    void resync(Source source);
    /// Verify mode: the vertical spacing of every line is current.
    void verifySpacing(const char* where);
    void syncSelection();
    void sanitizeTypingFormat();
    void reload();

    StyleOptions docStyle() const;
    Line lineAt(int index) const;
    AttributeMap inlineAt(int pos) const;
    /// Line attributes the text of `block` was styled for.
    AttributeMap styledFor(const QTextBlock& block) const;
    /// Inline attributes of the text typed next at the cursor.
    AttributeMap typingAttributes() const;
    AttributeMap resolveLine(const AttributeMap& current, const AttributeMap& requested) const;
    Delta lineFormatChange(int index, int length, const AttributeMap& attrs) const;
    int clampIndex(int index) const;

    // Keyboard (QuillEditorKeys.cpp)
    void handleEnter(bool plain);
    bool handleBackspace();
    bool handleDelete();
    /// Deletes [index, index + length) as a key press would (flutter_quill's
    /// delete rules: the last "\n" stays, a line merged by deleting its
    /// upper newline keeps the upper line's format).
    void deleteRange(int index, int length);
    /// Deletes a selection spanning lines as a Delta (QTextCursor would keep
    /// the first block's format); true when there was such a selection.
    bool deleteMultiLineSelection();
    bool handleTab(bool back);
    bool handleSpace();
    void toggle(const QString& name);

    // Clipboard
    Delta htmlToDelta(const QString& html) const;
    Delta plainTextChange(const Range& r, const QString& text) const;
    /// The change that replaces `r` with the clipboard or drop contents.
    Delta pasteChange(const QMimeData* source, const Range& r) const;

    /// The context menu at viewport position `pos`, with Undo / Redo wired
    /// to the History when one is installed. The caller owns it.
    QMenu* contextMenu(const QPoint& pos);

    // Image resizing (ImageResize.cpp)
    /// Index of the image whose handles are shown (a selection of just a
    /// resizable embed), or -1.
    int selectedEmbed() const;
    bool isResizable(int index) const;
    /// The rect the embed at `index` is drawn in, viewport coordinates;
    /// `object` gets the whole object, including spacing below the embed.
    QRectF embedRect(int index, QRectF* object = nullptr) const;
    /// Corner handle (0 top-left, 1 top-right, 2 bottom-right, 3 bottom-left)
    /// of the selected embed at viewport position `pos`, or -1.
    int handleAt(const QPoint& pos) const;
    /// The embed's size without size attributes, kept within the line.
    QSizeF naturalSize(int index) const;
    qreal availableWidth(int index) const;
    bool resizePress(QMouseEvent* e);
    bool resizeMove(QMouseEvent* e);
    bool resizeRelease(QMouseEvent* e);
    bool resizeKey(QKeyEvent* e);
    void paintResizeFrame(QPainter& p) const;
    void addResizeActions(QMenu* menu, const QPoint& pos);

    struct Resize
    {
        bool enabled = true;
        QuillEditor::ImageSizeAttributes spelling = QuillEditor::ImageSizeAttributes::Auto;
        bool pressed = false; ///< a press selected an embed; its release is ours too
        int index = -1;       ///< embed being dragged
        int handle = -1;
        QRectF start;   ///< its rect when the drag started
        QRectF preview; ///< the size it would get
        qreal maxWidth = 0;
        int framed = -1;     ///< embed whose frame was painted last
        int caretWidth = -1; ///< the caret's width while hidden over a selected image
    } resize;

    /// Activates the embed at `index` (handler, then embedActivated).
    void activateEmbed(int index);
    /// Handler and context of the embed at `index`.
    std::pair<EmbedRenderer*, EmbedContext> embedInfo(int index) const;

    QuillEditor* q;
    EmbedRegistry* embeds = nullptr;
    Delta contents = Delta().insert(QStringLiteral("\n"));
    StyleOptions style;
    QStringList pasteIgnored;
    QuillEditor::EditingRules rules = QuillEditor::EditingRules::FlutterQuill;
    History* history = nullptr;
    RemoteCursors* cursors = nullptr;

    int depth = 0;
    Source source = Source::User;
    QList<Delta> log;
    bool suppress = false;
    bool loading = false; ///< in setContents
    int lastUndoSteps = 0;
    int lastRedoSteps = 0;
    bool commandAdded = false;
    bool verify = false;
    int failures = 0;

    Range lastRange;
    Source selectionSource = Source::User;
    bool sanitizing = false;
    bool markerPressed = false;
    QPoint pressPos;
};

} // namespace quill
