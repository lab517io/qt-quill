#include "quill/RemoteCursors.h"

#include "quill/QuillEditor.h"

#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>

namespace quill {

namespace {

constexpr int kCaretWidth = 2;
constexpr int kSelectionAlpha = 64; // ~25 %

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace

RemoteCursors::RemoteCursors(QuillEditor* editor) : QObject(editor), m_editor(editor), m_flagTimer(new QTimer(this))
{
    m_flagTimer->setSingleShot(true);
    connect(m_flagTimer, &QTimer::timeout, this, [this] {
        m_editor->viewport()->update();
        scheduleFlagRefresh();
    });
    connect(editor, &QuillEditor::textChange, this, [this](const Delta& change) { transform(change); });
}

RemoteCursors::~RemoteCursors() = default;

RemoteCursors::Entry* RemoteCursors::find(const QString& id)
{
    for (Entry& e : m_entries) {
        if (e.cursor.id == id)
            return &e;
    }
    return nullptr;
}

const RemoteCursors::Entry* RemoteCursors::find(const QString& id) const
{
    return const_cast<RemoteCursors*>(this)->find(id);
}

void RemoteCursors::setCursor(const QString& id, const QString& name, const QColor& color, Range range)
{
    const int last = std::max(0, m_editor->getLength() - 1);
    range.index = std::clamp(range.index, 0, last);
    range.length = std::clamp(range.length, 0, last - range.index);
    Entry* e = find(id);
    QRect dirty;
    if (e) {
        dirty = boundsOf(*e);
    } else {
        m_entries.append(Entry());
        e = &m_entries.last();
    }
    e->cursor = {id, name, color, range};
    e->movedAt = now();
    repaint(dirty.united(boundsOf(*e)));
    scheduleFlagRefresh();
}

void RemoteCursors::removeCursor(const QString& id)
{
    for (qsizetype i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).cursor.id == id) {
            const QRect dirty = boundsOf(m_entries.at(i));
            m_entries.removeAt(i);
            repaint(dirty);
            return;
        }
    }
}

void RemoteCursors::clear()
{
    m_entries.clear();
    m_editor->viewport()->update();
}

QList<RemoteCursors::Cursor> RemoteCursors::cursors() const
{
    QList<Cursor> out;
    for (const Entry& e : m_entries)
        out.append(e.cursor);
    return out;
}

std::optional<RemoteCursors::Cursor> RemoteCursors::cursor(const QString& id) const
{
    const Entry* e = find(id);
    return e ? std::optional<Cursor>(e->cursor) : std::nullopt;
}

bool RemoteCursors::flagVisible(const QString& id) const
{
    const Entry* e = find(id);
    return e && (id == m_hovered || now() - e->movedAt < m_flagTimeout);
}

void RemoteCursors::transform(const Delta& change)
{
    if (m_entries.isEmpty())
        return;
    const int last = std::max(0, m_editor->getLength() - 1);
    for (Entry& e : m_entries) {
        Range& r = e.cursor.range;
        const int start = std::min(int(change.transformPosition(r.index)), last);
        const int end = std::min(int(change.transformPosition(r.index + r.length)), last);
        r = {start, std::max(0, end - start)};
    }
    m_editor->viewport()->update();
}

QRect RemoteCursors::caretRectAt(int pos) const
{
    QTextCursor c(m_editor->document());
    c.setPosition(std::clamp(pos, 0, std::max(0, m_editor->getLength() - 1)));
    const QRect r = m_editor->cursorRect(c);
    return QRect(r.left() - kCaretWidth / 2, r.top(), kCaretWidth, r.height());
}

QRect RemoteCursors::caretRect(const QString& id) const
{
    const Entry* e = find(id);
    return e ? caretRectAt(e->cursor.range.index + e->cursor.range.length) : QRect();
}

QRect RemoteCursors::flagRect(const Entry& e) const
{
    const QRect caret = caretRectAt(e.cursor.range.index + e.cursor.range.length);
    const QFontMetrics fm(m_editor->font());
    const int h = fm.height() * 3 / 4 + 4;
    QFont f = m_editor->font();
    f.setPixelSize(std::max(8, fm.height() * 3 / 4 - 2));
    const int w = QFontMetrics(f).horizontalAdvance(e.cursor.name) + 8;
    // Above the caret; below it on the first line when there is no room.
    const int top = caret.top() - h >= 0 ? caret.top() - h : caret.bottom() + 1;
    return QRect(caret.left(), top, w, h);
}

QList<QRectF> RemoteCursors::selectionRects(Range r) const
{
    QList<QRectF> out;
    if (r.length <= 0)
        return out;
    QTextDocument* doc = m_editor->document();
    QAbstractTextDocumentLayout* layout = doc->documentLayout();
    const QPointF offset(-m_editor->horizontalScrollBar()->value(), -m_editor->verticalScrollBar()->value());
    const int end = r.index + r.length;
    const int viewportBottom = m_editor->viewport()->height();
    for (QTextBlock b = doc->findBlock(r.index); b.isValid() && b.position() < end; b = b.next()) {
        const QRectF br = layout->blockBoundingRect(b).translated(offset);
        if (br.top() > viewportBottom)
            break;
        if (br.bottom() < 0 || !b.layout())
            continue;
        const int from = std::max(r.index, b.position()) - b.position();
        const int to = std::min(end, b.position() + b.length() - 1) - b.position();
        const bool includesNewline = end > b.position() + b.length() - 1;
        for (int i = 0; i < b.layout()->lineCount(); ++i) {
            const QTextLine line = b.layout()->lineAt(i);
            const int a = std::max(from, line.textStart());
            const int z = std::min(to, line.textStart() + line.textLength());
            const bool lastLine = i == b.layout()->lineCount() - 1;
            if (a > z || (a == z && !(includesNewline && lastLine)))
                continue;
            const qreal x1 = line.cursorToX(a);
            const qreal x2 = line.cursorToX(z);
            qreal left = std::min(x1, x2), width = std::abs(x2 - x1);
            if (includesNewline && lastLine)
                width += 4; // the selected line break
            out.append(QRectF(br.left() + left, br.top() + line.y(), width, line.height()));
        }
    }
    return out;
}

QRect RemoteCursors::boundsOf(const Entry& e) const
{
    QRect r = caretRectAt(e.cursor.range.index + e.cursor.range.length).adjusted(-1, 0, 1, 0);
    r = r.united(flagRect(e));
    for (const QRectF& s : selectionRects(e.cursor.range))
        r = r.united(s.toAlignedRect());
    return r;
}

void RemoteCursors::repaint(const QRect& r) const
{
    if (!r.isEmpty())
        m_editor->viewport()->update(r.adjusted(-2, -2, 2, 2));
}

void RemoteCursors::scheduleFlagRefresh()
{
    // Wake up when the next visible flag expires.
    qint64 next = -1;
    const qint64 t = now();
    for (const Entry& e : m_entries) {
        const qint64 expires = e.movedAt + m_flagTimeout;
        if (expires > t && (next < 0 || expires < next))
            next = expires;
    }
    if (next >= 0)
        m_flagTimer->start(int(next - t) + 10);
}

void RemoteCursors::hover(const QPoint& pos)
{
    QString hovered;
    for (const Entry& e : m_entries) {
        const QRect caret = caretRectAt(e.cursor.range.index + e.cursor.range.length).adjusted(-3, 0, 3, 0);
        if (caret.contains(pos) || (hovered.isEmpty() && flagVisible(e.cursor.id) && flagRect(e).contains(pos)))
            hovered = e.cursor.id;
    }
    if (hovered == m_hovered)
        return;
    const QString old = m_hovered;
    m_hovered = hovered;
    for (const QString& id : {old, hovered}) {
        if (const Entry* e = find(id))
            repaint(boundsOf(*e));
    }
}

void RemoteCursors::paint(QPainter& p) const
{
    if (m_entries.isEmpty())
        return;
    p.save();
    QFont flagFont = m_editor->font();
    const QFontMetrics fm(m_editor->font());
    flagFont.setPixelSize(std::max(8, fm.height() * 3 / 4 - 2));
    for (const Entry& e : m_entries) {
        QColor tint = e.cursor.color;
        tint.setAlpha(kSelectionAlpha);
        for (const QRectF& r : selectionRects(e.cursor.range))
            p.fillRect(r, tint);
    }
    for (const Entry& e : m_entries)
        p.fillRect(caretRectAt(e.cursor.range.index + e.cursor.range.length), e.cursor.color);
    for (const Entry& e : m_entries) {
        if (!flagVisible(e.cursor.id) || e.cursor.name.isEmpty())
            continue;
        const QRect flag = flagRect(e);
        p.fillRect(flag, e.cursor.color);
        p.setFont(flagFont);
        p.setPen(qGray(e.cursor.color.rgb()) > 150 ? Qt::black : Qt::white);
        p.drawText(flag.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, e.cursor.name);
    }
    p.restore();
}

} // namespace quill
