#include "NotesEmbed.h"

#include <quill/Delta.h>
#include <quill/QuillEditor.h>

#include <QFontMetricsF>
#include <QInputDialog>
#include <QPainter>
#include <QPainterPath>
#include <QTextDocument>

namespace {
constexpr int kMaxLines = 6;
constexpr qreal kPad = 10;

QStringList lines(const quill::EmbedContext& c)
{
    QStringList l = NotesEmbed::noteText(c).split(QLatin1Char('\n'));
    while (!l.isEmpty() && l.last().isEmpty())
        l.removeLast();
    if (l.size() > kMaxLines) {
        l = l.mid(0, kMaxLines);
        l.last() += QChar(0x2026);
    }
    return l;
}
} // namespace

NotesEmbed::NotesEmbed() : CustomEmbedRenderer(QStringLiteral("notes")) {}

QString NotesEmbed::noteText(const quill::EmbedContext& c)
{
    const auto parsed = parse(c.data);
    if (!parsed)
        return {};
    const auto delta = quill::Delta::fromJson(parsed->second.toString().toUtf8());
    if (!delta)
        return parsed->second.toString();
    QString text;
    for (const quill::Op& op : delta->ops())
        text += op.isTextInsert() ? op.text() : QString(QChar::ObjectReplacementCharacter);
    return text;
}

QSizeF NotesEmbed::size(const quill::EmbedContext& c) const
{
    const QFontMetricsF fm(c.document->defaultFont());
    const qreal width = std::min(c.availableWidth > 0 ? c.availableWidth : 480.0, 480.0);
    return QSizeF(width, 2 * kPad + fm.height() * (1.4 + std::max<qsizetype>(1, lines(c).size())));
}

void NotesEmbed::paint(QPainter* p, const QRectF& rect, const quill::EmbedContext& c) const
{
    p->setRenderHint(QPainter::Antialiasing);
    QPainterPath card;
    card.addRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p->fillPath(card, QColor(0xFF, 0xF8, 0xE1));
    p->setPen(QPen(QColor(0xFF, 0xB3, 0x00), 1));
    p->drawPath(card);
    p->fillRect(QRectF(rect.left() + 1, rect.top() + 6, 4, rect.height() - 12), QColor(0xFF, 0xB3, 0x00));

    QFont font = c.document->defaultFont();
    const QFontMetricsF fm(font);
    QFont bold = font;
    bold.setBold(true);
    p->setFont(bold);
    p->setPen(QColor(0x5D, 0x40, 0x37));
    qreal y = rect.top() + kPad;
    const qreal x = rect.left() + kPad + 6, w = rect.width() - 2 * kPad - 6;
    p->drawText(QRectF(x, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, QObject::tr("Notes"));
    y += fm.height() * 1.4;
    p->setFont(font);
    p->setPen(QColor(0x21, 0x21, 0x21));
    for (const QString& line : lines(c)) {
        p->drawText(QRectF(x, y, w, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                    fm.elidedText(line, Qt::ElideRight, w));
        y += fm.height();
    }
}

bool NotesEmbed::activate(quill::QuillEditor* editor, const quill::EmbedContext& c)
{
    if (editor->isReadOnly())
        return false;
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(editor, QObject::tr("Edit note"), QObject::tr("Note:"),
                                                  noteText(c).chopped(noteText(c).endsWith(QLatin1Char('\n')) ? 1 : 0), &ok);
    if (!ok)
        return true;
    if (!text.endsWith(QLatin1Char('\n')))
        text += QLatin1Char('\n');
    const QString note = QString::fromUtf8(quill::Delta().insert(text).toJson());
    const QString value = encode(customType(), note);
    editor->updateContents(quill::Delta().retain(c.position).remove(1).insert(
                               QJsonObject{{QStringLiteral("custom"), value}}, c.attributes),
                           quill::Source::User);
    return true;
}

QString NotesEmbed::toolTip(const quill::EmbedContext&) const { return QObject::tr("Click to edit the note"); }
