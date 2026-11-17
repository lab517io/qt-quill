#pragma once

#include "quill/Range.h"

#include <QColor>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include <optional>

class QPainter;
class QTimer;

namespace quill {

class Delta;
class QuillEditor;

/// Other users' cursors and selections, painted over a QuillEditor: a
/// caret in the user's colour, the selection tinted with it, and a flag with
/// the user's name while the cursor moves (flagTimeout) or the mouse is
/// over it.
///
/// Every change of the editor's text moves the cursors (transformPosition),
/// so they stay on the right characters between the host's updates. The
/// host feeds them from its own protocol with setCursor() / removeCursor(),
/// in indices of the local document (see OtClient::transformFromServer).
///
/// Obtained from QuillEditor::remoteCursors().
class RemoteCursors : public QObject
{
    Q_OBJECT
public:
    struct Cursor
    {
        QString id;
        QString name;
        QColor color;
        Range range;
    };

    /// Use QuillEditor::remoteCursors().
    explicit RemoteCursors(QuillEditor* editor);
    ~RemoteCursors() override;

    /// Adds or moves the cursor `id`.
    void setCursor(const QString& id, const QString& name, const QColor& color, Range range);
    void removeCursor(const QString& id);
    void clear();
    QList<Cursor> cursors() const;
    std::optional<Cursor> cursor(const QString& id) const;

    /// How long a name flag stays up after its cursor moved (ms; default 2000).
    int flagTimeout() const { return m_flagTimeout; }
    void setFlagTimeout(int ms) { m_flagTimeout = ms; }
    /// Whether the name flag of `id` is shown now.
    bool flagVisible(const QString& id) const;

    /// The caret of `id` in viewport coordinates (empty when unknown).
    QRect caretRect(const QString& id) const;

    /// Moves every cursor past `change` (done for each textChange).
    void transform(const Delta& change);

    // Called by QuillEditor.
    void paint(QPainter& painter) const;
    void hover(const QPoint& viewportPos);

private:
    struct Entry
    {
        Cursor cursor;
        qint64 movedAt = 0;
    };

    QRect caretRectAt(int pos) const;
    QRect flagRect(const Entry& e) const;
    QList<QRectF> selectionRects(Range r) const;
    QRect boundsOf(const Entry& e) const;
    void repaint(const QRect& r) const;
    void scheduleFlagRefresh();
    Entry* find(const QString& id);
    const Entry* find(const QString& id) const;

    QuillEditor* m_editor;
    QList<Entry> m_entries;
    int m_flagTimeout = 2000;
    QString m_hovered;
    QTimer* m_flagTimer;
};

} // namespace quill
