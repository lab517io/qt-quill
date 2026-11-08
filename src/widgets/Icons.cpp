#include "Icons.h"

#include <QFont>
#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

#include <functional>

namespace quill::detail {
namespace {

// All glyphs are drawn on a 24 x 24 grid.
using Draw = std::function<void(QPainter&, const QColor&)>;

QPen pen(const QColor& c, qreal w = 2) { return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin); }

void letter(QPainter& p, const QColor& c, const QString& text, qreal size, bool bold = true, bool italic = false,
            const QRectF& box = QRectF(0, 0, 24, 24))
{
    QFont f(QStringLiteral("DejaVu Sans"));
    f.setPixelSize(int(size));
    f.setBold(bold);
    f.setItalic(italic);
    p.setFont(f);
    p.setPen(c);
    p.drawText(box, Qt::AlignCenter, text);
}

void lines(QPainter& p, const QColor& c, const QList<std::pair<qreal, qreal>>& spans, qreal top = 6, qreal step = 4)
{
    p.setPen(pen(c));
    qreal y = top;
    for (const auto& [x1, x2] : spans) {
        p.drawLine(QPointF(x1, y), QPointF(x2, y));
        y += step;
    }
}

void arrow(QPainter& p, const QColor& c, bool redo)
{
    p.save();
    if (redo) {
        p.translate(24, 0);
        p.scale(-1, 1);
    }
    p.setPen(pen(c));
    QPainterPath path;
    path.moveTo(5, 10);
    path.lineTo(15, 10);
    path.cubicTo(22, 10, 22, 20, 15, 20);
    path.lineTo(9, 20);
    p.drawPath(path);
    p.drawLine(QPointF(5, 10), QPointF(9, 6));
    p.drawLine(QPointF(5, 10), QPointF(9, 14));
    p.restore();
}

const QHash<QString, Draw>& glyphs()
{
    static const QHash<QString, Draw> map{
        {QStringLiteral("undo"), [](QPainter& p, const QColor& c) { arrow(p, c, false); }},
        {QStringLiteral("redo"), [](QPainter& p, const QColor& c) { arrow(p, c, true); }},
        {QStringLiteral("bold"), [](QPainter& p, const QColor& c) { letter(p, c, QStringLiteral("B"), 17); }},
        {QStringLiteral("italic"), [](QPainter& p, const QColor& c) { letter(p, c, QStringLiteral("I"), 17, false, true); }},
        {QStringLiteral("underline"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("U"), 15, false, false, QRectF(0, -2, 24, 24));
             lines(p, c, {{6, 18}}, 20.5);
         }},
        {QStringLiteral("strike"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("S"), 16, false);
             lines(p, c, {{4, 20}}, 12.5);
         }},
        {QStringLiteral("code"), [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawPolyline(QPolygonF{{9, 7}, {4, 12}, {9, 17}});
             p.drawPolyline(QPolygonF{{15, 7}, {20, 12}, {15, 17}});
         }},
        {QStringLiteral("subscript"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("x"), 15, false, false, QRectF(1, -1, 15, 22));
             letter(p, c, QStringLiteral("2"), 9, true, false, QRectF(14, 11, 9, 11));
         }},
        {QStringLiteral("superscript"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("x"), 15, false, false, QRectF(1, 3, 15, 22));
             letter(p, c, QStringLiteral("2"), 9, true, false, QRectF(14, 1, 9, 11));
         }},
        {QStringLiteral("small"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("A"), 16, false, false, QRectF(0, 0, 15, 24));
             letter(p, c, QStringLiteral("a"), 10, false, false, QRectF(13, 5, 10, 18));
         }},
        {QStringLiteral("color"),
         [](QPainter& p, const QColor& c) { letter(p, c, QStringLiteral("A"), 15, true, false, QRectF(0, -3, 24, 22)); }},
        {QStringLiteral("background"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             QPainterPath pen1;
             pen1.moveTo(8, 15);
             pen1.lineTo(15, 4);
             pen1.lineTo(19, 7);
             pen1.lineTo(12, 18);
             pen1.closeSubpath();
             p.drawPath(pen1);
             p.drawLine(QPointF(8, 15), QPointF(6, 18));
         }},
        {QStringLiteral("clear"),
         [](QPainter& p, const QColor& c) {
             letter(p, c, QStringLiteral("T"), 15, false, false, QRectF(-2, -1, 20, 22));
             p.setPen(pen(c));
             p.drawLine(QPointF(14, 14), QPointF(20, 20));
             p.drawLine(QPointF(20, 14), QPointF(14, 20));
         }},
        {QStringLiteral("list-bullet"),
         [](QPainter& p, const QColor& c) {
             p.setPen(Qt::NoPen);
             p.setBrush(c);
             for (int i = 0; i < 3; ++i)
                 p.drawEllipse(QPointF(5, 6 + 6 * i), 1.8, 1.8);
             lines(p, c, {{10, 20}, {10, 20}, {10, 20}}, 6, 6);
         }},
        {QStringLiteral("list-ordered"),
         [](QPainter& p, const QColor& c) {
             for (int i = 0; i < 3; ++i)
                 letter(p, c, QString::number(i + 1), 7, true, false, QRectF(1, 2 + 6 * i, 7, 8));
             lines(p, c, {{10, 20}, {10, 20}, {10, 20}}, 6, 6);
         }},
        {QStringLiteral("list-check"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c, 1.5));
             p.drawRect(QRectF(3, 4, 5, 5));
             p.drawRect(QRectF(3, 14, 5, 5));
             p.drawPolyline(QPolygonF{{4, 16.5}, {5.5, 18}, {9, 13}});
             lines(p, c, {{11, 20}, {11, 20}}, 6.5, 10);
         }},
        {QStringLiteral("indent"),
         [](QPainter& p, const QColor& c) {
             lines(p, c, {{4, 20}, {11, 20}, {11, 20}, {4, 20}}, 5, 4.6);
             p.setPen(pen(c, 1.5));
             p.drawPolyline(QPolygonF{{4, 9}, {7, 12}, {4, 15}});
         }},
        {QStringLiteral("outdent"),
         [](QPainter& p, const QColor& c) {
             lines(p, c, {{4, 20}, {11, 20}, {11, 20}, {4, 20}}, 5, 4.6);
             p.setPen(pen(c, 1.5));
             p.drawPolyline(QPolygonF{{7, 9}, {4, 12}, {7, 15}});
         }},
        {QStringLiteral("align-left"), [](QPainter& p, const QColor& c) { lines(p, c, {{4, 20}, {4, 14}, {4, 20}, {4, 14}}, 5, 4.6); }},
        {QStringLiteral("align-center"), [](QPainter& p, const QColor& c) { lines(p, c, {{4, 20}, {7, 17}, {4, 20}, {7, 17}}, 5, 4.6); }},
        {QStringLiteral("align-right"), [](QPainter& p, const QColor& c) { lines(p, c, {{4, 20}, {10, 20}, {4, 20}, {10, 20}}, 5, 4.6); }},
        {QStringLiteral("align-justify"), [](QPainter& p, const QColor& c) { lines(p, c, {{4, 20}, {4, 20}, {4, 20}, {4, 20}}, 5, 4.6); }},
        {QStringLiteral("direction-rtl"),
         [](QPainter& p, const QColor& c) {
             lines(p, c, {{9, 20}, {9, 20}}, 6, 5);
             p.setPen(pen(c));
             p.drawLine(QPointF(20, 18), QPointF(4, 18));
             p.drawPolyline(QPolygonF{{7, 15}, {4, 18}, {7, 21}});
         }},
        {QStringLiteral("blockquote"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c, 3));
             p.drawLine(QPointF(5, 5), QPointF(5, 19));
             lines(p, c, {{10, 20}, {10, 17}, {10, 20}}, 7, 5);
         }},
        {QStringLiteral("code-block"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c, 1.5));
             p.drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
             p.drawPolyline(QPolygonF{{9, 9}, {6.5, 12}, {9, 15}});
             p.drawPolyline(QPolygonF{{15, 9}, {17.5, 12}, {15, 15}});
         }},
        {QStringLiteral("link"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawRoundedRect(QRectF(3, 9, 10, 6), 3, 3);
             p.drawRoundedRect(QRectF(11, 9, 10, 6), 3, 3);
         }},
        {QStringLiteral("image"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c, 1.5));
             p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
             p.drawPolyline(QPolygonF{{4, 17}, {10, 11}, {14, 15}, {16, 13}, {20, 17}});
             p.setBrush(c);
             p.drawEllipse(QPointF(16, 9), 1.5, 1.5);
         }},
        {QStringLiteral("video"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c, 1.5));
             p.drawRoundedRect(QRectF(3, 5, 18, 14), 2, 2);
             p.setBrush(c);
             p.drawPolygon(QPolygonF{{10, 9}, {15, 12}, {10, 15}});
         }},
        {QStringLiteral("formula"),
         [](QPainter& p, const QColor& c) { letter(p, c, QStringLiteral("∑"), 17, false); }},
        {QStringLiteral("search"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
             p.drawLine(QPointF(14, 14), QPointF(20, 20));
         }},
        {QStringLiteral("close"),
         [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawLine(QPointF(7, 7), QPointF(17, 17));
             p.drawLine(QPointF(17, 7), QPointF(7, 17));
         }},
        {QStringLiteral("previous"), [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawPolyline(QPolygonF{{7, 15}, {12, 9}, {17, 15}});
         }},
        {QStringLiteral("next"), [](QPainter& p, const QColor& c) {
             p.setPen(pen(c));
             p.drawPolyline(QPolygonF{{7, 9}, {12, 15}, {17, 9}});
         }},
    };
    return map;
}

class GlyphEngine : public QIconEngine
{
public:
    explicit GlyphEngine(Draw draw) : m_draw(std::move(draw)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette pal = QGuiApplication::palette();
        QColor c = pal.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        if (mode == QIcon::Selected)
            c = pal.color(QPalette::HighlightedText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        const qreal side = std::min(rect.width(), rect.height());
        painter->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        painter->scale(side / 24.0, side / 24.0);
        m_draw(*painter, c);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine* clone() const override { return new GlyphEngine(m_draw); }

private:
    Draw m_draw;
};

} // namespace

QIcon toolbarIcon(const QString& name)
{
    const auto it = glyphs().constFind(name);
    if (it == glyphs().constEnd())
        return {};
    return QIcon(new GlyphEngine(*it));
}

QStringList toolbarIconNames() { return glyphs().keys(); }

} // namespace quill::detail
