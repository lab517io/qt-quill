// Resize handles for images (milestone M13): a click selects an image, a
// corner drag previews the new size as an overlay and commits it once, as
// the attributes flutter_quill_extensions (or Quill.js) write.

#include "QuillEditor_p.h"

#include "ImageSize.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <cmath>

namespace quill {

namespace {

constexpr qreal kHandle = 8;   // drawn size of a corner handle
constexpr qreal kMinSize = 16; // smallest size a drag gives

QTextCharFormat formatAt(QTextDocument* doc, int index)
{
    QTextCursor c(doc);
    c.setPosition(index + 1);
    return c.charFormat();
}

bool isEmbedFormat(const QTextCharFormat& f)
{
    return f.hasProperty(prop::EmbedType) || f.isImageFormat();
}

QPointF corner(const QRectF& r, int handle)
{
    switch (handle) {
    case 0:
        return r.topLeft();
    case 1:
        return r.topRight();
    case 2:
        return r.bottomRight();
    default:
        return r.bottomLeft();
    }
}

} // namespace

// --- QuillEditor -----------------------------------------------------------------

void QuillEditor::setImageResizeEnabled(bool on)
{
    d->resize.enabled = on;
    viewport()->update();
}

bool QuillEditor::isImageResizeEnabled() const
{
    return d->resize.enabled;
}

QuillEditor::ImageSizeAttributes QuillEditor::imageSizeAttributes() const
{
    return d->resize.spelling;
}

void QuillEditor::setImageSizeAttributes(ImageSizeAttributes spelling)
{
    d->resize.spelling = spelling;
}

Delta QuillEditor::resizeEmbed(int index, const QSizeF& size, Source source)
{
    if (index < 0 || index >= getLength() - 1 || !isEmbedFormat(formatAt(document(), index)))
        return {};
    const Delta op = d->contents.slice(index, index + 1);
    const AttributeMap current = op.ops().isEmpty() ? AttributeMap() : op.ops().first().attributes();
    std::optional<QSizeF> wanted;
    if (size.isValid())
        wanted = size;
    const AttributeMap change =
        detail::resizedImageAttributes(current, wanted, detail::SizeSpelling(int(d->resize.spelling)));
    if (change.isEmpty())
        return {};
    return formatText(index, 1, change, source);
}

// --- Geometry ----------------------------------------------------------------------

bool QuillEditorPrivate::isResizable(int index) const
{
    QTextDocument* doc = q->document();
    if (index < 0 || index >= doc->characterCount() - 1 || doc->characterAt(index) != QChar::ObjectReplacementCharacter)
        return false;
    if (!isEmbedFormat(formatAt(doc, index)))
        return false;
    return embedInfo(index).first->resizable();
}

int QuillEditorPrivate::selectedEmbed() const
{
    if (!resize.enabled || q->isReadOnly())
        return -1;
    const QTextCursor c = q->textCursor();
    if (std::abs(c.anchor() - c.position()) != 1)
        return -1;
    const int index = c.selectionStart();
    return isResizable(index) ? index : -1;
}

QRectF QuillEditorPrivate::embedRect(int index, QRectF* object) const
{
    QTextDocument* doc = q->document();
    const QTextBlock b = doc->findBlock(index);
    if (!b.isValid() || !b.layout())
        return {};
    const int rel = index - b.position();
    const QTextLine line = b.layout()->lineForTextPosition(rel);
    if (!line.isValid())
        return {};
    const QTextCharFormat f = formatAt(doc, index);
    EmbedRegistry* registry = EmbedRegistry::registryOf(doc);
    if (!registry)
        registry = embeds;
    const QSizeF drawn = registry->drawnSize(doc, index, f);
    // Objects sit on the baseline; the layout may make them taller than
    // drawn (spacing below an image alone on its line).
    QSizeF objectSize = drawn;
    if (QTextObjectInterface* handler = doc->documentLayout()->handlerForObject(f.objectType()))
        objectSize = handler->intrinsicSize(doc, index, f);
    const QRectF block = doc->documentLayout()->blockBoundingRect(b);
    const qreal x1 = line.cursorToX(rel), x2 = line.cursorToX(rel + 1);
    const QPointF offset(-q->horizontalScrollBar()->value(), -q->verticalScrollBar()->value());
    const qreal top = block.top() + line.y() + line.ascent() - objectSize.height();
    const QRectF rect = QRectF(block.left() + std::min(x1, x2), top, drawn.width(), drawn.height()).translated(offset);
    if (object)
        *object = QRectF(rect.topLeft(), QSizeF(drawn.width(), objectSize.height()));
    return rect;
}

int QuillEditorPrivate::handleAt(const QPoint& pos) const
{
    const int index = selectedEmbed();
    if (index < 0)
        return -1;
    const QRectF r = embedRect(index);
    // Larger than drawn, so the handles stay easy to hit (also at HiDPI).
    const qreal hit = std::max<qreal>(12, kHandle * q->devicePixelRatioF());
    for (int h = 0; h < 4; ++h) {
        const QPointF c = corner(r, h);
        if (QRectF(c.x() - hit / 2, c.y() - hit / 2, hit, hit).contains(pos))
            return h;
    }
    return -1;
}

qreal QuillEditorPrivate::availableWidth(int index) const
{
    QTextDocument* doc = q->document();
    EmbedRegistry* registry = EmbedRegistry::registryOf(doc);
    if (!registry)
        registry = embeds;
    return registry->context(doc, index, formatAt(doc, index)).availableWidth;
}

QSizeF QuillEditorPrivate::naturalSize(int index) const
{
    auto [handler, c] = embedInfo(index);
    for (const char* k : {"width", "height", "style"})
        c.attributes.remove(QString::fromLatin1(k));
    if (c.format.isImageFormat()) {
        c.format.clearProperty(QTextFormat::ImageWidth);
        c.format.clearProperty(QTextFormat::ImageHeight);
    }
    QSizeF s = handler->size(c);
    if (c.availableWidth > 0 && s.width() > c.availableWidth)
        s = QSizeF(c.availableWidth, s.height() * c.availableWidth / s.width());
    return s;
}

// --- Mouse and keys ------------------------------------------------------------------

bool QuillEditorPrivate::resizePress(QMouseEvent* e)
{
    if (!resize.enabled || q->isReadOnly())
        return false;
    if (e->button() == Qt::LeftButton) {
        const int h = handleAt(e->pos());
        if (h >= 0) {
            resize.index = selectedEmbed();
            resize.handle = h;
            resize.start = embedRect(resize.index);
            resize.preview = resize.start;
            resize.maxWidth = availableWidth(resize.index);
            e->accept();
            return true;
        }
    }
    if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton)
        return false;
    const int at = q->embedAt(e->pos());
    if (at < 0 || !isResizable(at))
        return false;
    // A click on an image selects it (Quill), rather than placing the caret.
    q->setSelection(at, 1, Source::User);
    resize.pressed = e->button() == Qt::LeftButton;
    q->viewport()->update();
    e->accept();
    return true;
}

bool QuillEditorPrivate::resizeMove(QMouseEvent* e)
{
    if (resize.index < 0) {
        if (!q->isReadOnly() && resize.enabled) {
            const int h = handleAt(e->pos());
            if (h >= 0) {
                q->viewport()->setCursor(h == 0 || h == 2 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
                return true;
            }
        }
        return resize.pressed; // a press that selected an image does not drag-select
    }
    const QRectF& r = resize.start;
    const QPointF anchor = corner(r, (resize.handle + 2) % 4);
    const QPointF pos = e->position();
    const bool right = resize.handle == 1 || resize.handle == 2;
    const bool bottom = resize.handle == 2 || resize.handle == 3;
    // Extents from the fixed corner towards the handle; past it they are
    // negative and give the minimum size.
    qreal w = right ? pos.x() - anchor.x() : anchor.x() - pos.x();
    qreal h = bottom ? pos.y() - anchor.y() : anchor.y() - pos.y();
    const qreal maxWidth = std::max(kMinSize, resize.maxWidth > 0 ? resize.maxWidth : 1e6);
    if (e->modifiers() & Qt::ShiftModifier || r.height() <= 0 || r.width() <= 0) {
        w = std::clamp(w, kMinSize, maxWidth);
        h = std::max(h, kMinSize);
    } else {
        // Keep the aspect ratio, following the direction that changed more.
        const qreal aspect = r.width() / r.height();
        const qreal sx = w / r.width(), sy = h / r.height();
        qreal scale = std::abs(sx - 1) >= std::abs(sy - 1) ? sx : sy;
        scale = std::max(scale, kMinSize / std::min(r.width(), r.height()));
        w = std::min(r.width() * scale, maxWidth);
        h = w / aspect;
    }
    w = std::round(w);
    h = std::round(h);
    const QRectF preview(right ? anchor.x() : anchor.x() - w, bottom ? anchor.y() : anchor.y() - h, w, h);
    q->viewport()->update(resize.preview.united(preview).toAlignedRect().adjusted(-60, -30, 60, 30));
    resize.preview = preview;
    return true;
}

bool QuillEditorPrivate::resizeRelease(QMouseEvent* e)
{
    if (resize.index >= 0) {
        const int index = resize.index;
        const QSizeF size = resize.preview.size();
        const bool changed = size != resize.start.size();
        resize.index = -1;
        resize.handle = -1;
        if (changed)
            q->resizeEmbed(index, size, Source::User);
        q->setSelection(index, 1, Source::Silent);
        q->viewport()->update();
        e->accept();
        return true;
    }
    if (resize.pressed) {
        resize.pressed = false;
        e->accept();
        return true;
    }
    return false;
}

bool QuillEditorPrivate::resizeKey(QKeyEvent* e)
{
    if (e->key() != Qt::Key_Escape)
        return false;
    if (resize.index >= 0) {
        resize.index = -1;
        resize.handle = -1;
        q->viewport()->update();
        return true;
    }
    const int index = selectedEmbed();
    if (index < 0)
        return false;
    q->setSelection(index + 1, 0, Source::User);
    q->viewport()->update();
    return true;
}

// --- Painting and menu ----------------------------------------------------------------

void QuillEditorPrivate::paintResizeFrame(QPainter& p) const
{
    const int index = resize.index >= 0 ? resize.index : selectedEmbed();
    if (index < 0)
        return;
    QRectF object;
    const QRectF image = embedRect(index, &object);
    const QRectF r = resize.index >= 0 ? resize.preview : image;
    const QColor accent = q->palette().color(QPalette::Highlight);
    p.save();
    // Qt tints a selected object with the highlight colour; the frame shows
    // the selection, so draw the image again untinted.
    {
        auto [handler, c] = embedInfo(index);
        p.save();
        p.fillRect(object, q->viewport()->palette().color(q->viewport()->backgroundRole()));
        p.setPen(q->palette().color(QPalette::Text));
        handler->paint(&p, image, c);
        p.restore();
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    if (resize.index >= 0) {
        QColor shade = accent;
        shade.setAlpha(40);
        p.fillRect(r, shade);
        p.setPen(QPen(accent, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));
    } else {
        // 1 px strips just inside the edges: exact at any pixel ratio.
        p.fillRect(QRectF(r.left(), r.top(), r.width(), 1), accent);
        p.fillRect(QRectF(r.left(), r.bottom() - 1, r.width(), 1), accent);
        p.fillRect(QRectF(r.left(), r.top(), 1, r.height()), accent);
        p.fillRect(QRectF(r.right() - 1, r.top(), 1, r.height()), accent);
    }
    p.setPen(QPen(accent, 1));
    p.setBrush(q->palette().color(QPalette::Base));
    for (int h = 0; h < 4; ++h) {
        const QPointF c = corner(r, h);
        p.drawRect(QRectF(c.x() - kHandle / 2, c.y() - kHandle / 2, kHandle, kHandle));
    }
    // Size label, inside the bottom-right corner (outside when too small).
    const QString label = QStringLiteral("%1 × %2").arg(qRound(r.width())).arg(qRound(r.height()));
    QFont font = q->font();
    font.setPixelSize(11);
    const QFontMetricsF fm(font);
    const QSizeF box(fm.horizontalAdvance(label) + 8, fm.height() + 2);
    QRectF tag(r.right() - box.width() - 4, r.bottom() - box.height() - 4, box.width(), box.height());
    if (r.width() < box.width() + 8 || r.height() < box.height() + 8)
        tag.moveTopLeft(QPointF(r.right() + 4, r.bottom() - box.height()));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 170));
    p.drawRect(tag);
    p.setPen(Qt::white);
    p.setFont(font);
    p.drawText(tag, Qt::AlignCenter, label);
    p.restore();
}

void QuillEditorPrivate::addResizeActions(QMenu* menu, const QPoint& pos)
{
    Q_UNUSED(pos);
    const int index = selectedEmbed();
    if (index < 0)
        return;
    QAction* first = menu->actions().isEmpty() ? nullptr : menu->actions().first();
    auto* sub = new QMenu(QuillEditor::tr("Image size"), menu);
    const auto add = [&](const QString& text, const std::function<QSizeF()>& size) {
        QAction* a = sub->addAction(text);
        QObject::connect(a, &QAction::triggered, q, [this, index, size] {
            q->resizeEmbed(index, size(), Source::User);
            q->setSelection(index, 1, Source::Silent);
        });
    };
    add(QuillEditor::tr("Original size"), [] { return QSizeF(); });
    add(QuillEditor::tr("Fit width"), [this, index] {
        const QSizeF n = naturalSize(index);
        const qreal w = availableWidth(index);
        return n.width() > 0 ? QSizeF(w, n.height() * w / n.width()) : QSizeF();
    });
    for (const int percent : {25, 50, 100}) {
        add(QuillEditor::tr("%1 %").arg(percent), [this, index, percent] {
            QSizeF n = naturalSize(index) * (percent / 100.0);
            return QSizeF(std::max(kMinSize, n.width()), std::max(kMinSize, n.height()));
        });
    }
    QAction* entry = menu->insertMenu(first, sub);
    entry->setObjectName(QStringLiteral("image-size"));
    menu->insertSeparator(first);
}

} // namespace quill
