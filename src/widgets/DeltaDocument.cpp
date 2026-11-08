#include "quill/DeltaDocument.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextList>
#include <QUrl>

#include <vector>

namespace quill {
namespace {

QString compactJson(const QJsonValue& v)
{
    const QByteArray a = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}

AttributeMap inlineSubset(const AttributeMap& attrs)
{
    AttributeMap out;
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        if (formats::inlineKeys().contains(it.key()))
            out.insert(it.key(), it.value());
    }
    return out;
}

// Parses an image dimension the way flutter_quill_extensions does: a bare
// number or a CSS length in px / em / rem (16 px per em). Percentages and
// viewport units need a layout context and are ignored.
std::optional<double> parseLength(const QJsonValue& v)
{
    if (v.isDouble())
        return v.toDouble() > 0 ? std::optional<double>(v.toDouble()) : std::nullopt;
    QString s = v.toString().trimmed();
    if (s.isEmpty())
        return std::nullopt;
    double scale = 1.0;
    if (s.endsWith(QLatin1String("px"))) {
        s.chop(2);
    } else if (s.endsWith(QLatin1String("rem"))) {
        s.chop(3);
        scale = 16.0;
    } else if (s.endsWith(QLatin1String("em"))) {
        s.chop(2);
        scale = 16.0;
    }
    bool ok = false;
    const double d = s.trimmed().toDouble(&ok);
    return ok && d > 0 ? std::optional<double>(d * scale) : std::nullopt;
}

QHash<QString, QString> parseCss(const QString& css)
{
    QHash<QString, QString> out;
    for (const QString& decl : css.split(QLatin1Char(';'))) {
        const QStringList parts = decl.split(QLatin1Char(':'));
        if (parts.size() == 2)
            out.insert(parts.at(0).trimmed(), parts.at(1).trimmed());
    }
    return out;
}

class Loader
{
public:
    Loader(QTextDocument* doc, const LoadOptions& options, const StyleOptions& style)
        : m_doc(doc), m_options(options), m_style(style), m_cursor(doc)
    {
    }

    void run(const Delta& delta, LoadResult& result)
    {
        m_cursor.beginEditBlock();
        qsizetype lineIndex = 0;
        delta.eachLine([&](const Delta& line, const AttributeMap& lineAttrs, qsizetype i) {
            lineIndex = i;
            checkAttributes(lineAttrs, true, i, result);
            const QTextBlockFormat bf = formats::blockFormat(lineAttrs, m_style);
            const QTextCharFormat bcf = formats::charFormat(inlineSubset(lineAttrs), lineAttrs, m_style);
            if (i == 0) {
                m_cursor.setBlockFormat(bf);
                m_cursor.setBlockCharFormat(bcf);
            } else {
                m_cursor.insertBlock(bf, bcf);
            }
            for (const Op& op : line.ops()) {
                checkAttributes(op.attributes(), false, i, result);
                if (op.isEmbed())
                    insertEmbed(op, lineAttrs, i, result);
                else
                    m_cursor.insertText(op.text(), formats::charFormat(op.attributes(), lineAttrs, m_style));
            }
            return true;
        });
        Q_UNUSED(lineIndex);
        DeltaDocument::regroupLists(m_doc->firstBlock(), m_doc->lastBlock(), m_style);
        m_cursor.endEditBlock();
    }

private:
    void checkAttributes(const AttributeMap& attrs, bool line, qsizetype lineIndex, LoadResult& result)
    {
        for (auto it = attrs.begin(); it != attrs.end(); ++it) {
            const QString& key = it.key();
            if (!formats::inlineKeys().contains(key) && !formats::lineKeys().contains(key)
                && !formats::passiveKeys().contains(key)) {
                result.warnings.append(QStringLiteral("line %1: unknown attribute \"%2\" preserved")
                                           .arg(lineIndex + 1)
                                           .arg(key));
            }
        }
        for (const QString& problem : formats::validate(attrs, line, m_style))
            result.warnings.append(QStringLiteral("line %1: %2").arg(lineIndex + 1).arg(problem));
    }

    void insertEmbed(const Op& op, const AttributeMap& lineAttrs, qsizetype lineIndex, LoadResult& result)
    {
        const QJsonObject embed = op.embed();
        const QString type = embed.isEmpty() ? QString() : embed.begin().key();
        if (embed.size() != 1) {
            result.warnings.append(
                QStringLiteral("line %1: embed should have exactly one key: %2")
                    .arg(lineIndex + 1)
                    .arg(compactJson(embed)));
        }
        QTextCharFormat base = formats::charFormat(op.attributes(), lineAttrs, m_style);
        base.setProperty(prop::EmbedType, type);
        base.setProperty(prop::Embed, embed);

        const QJsonValue data = embed.value(type);
        if (type == QLatin1String("image") && data.isString()) {
            QTextImageFormat img;
            img.merge(base);
            const QString source = data.toString();
            img.setName(source);
            applyImageSize(img, op.attributes());
            registerImageResource(source);
            m_cursor.insertImage(img);
            return;
        }
        // Other embeds render through a QTextObjectInterface registered for
        // EmbedObjectType (milestone M7); until then they take no space.
        base.setObjectType(prop::EmbedObjectType);
        m_cursor.insertText(QString(QChar::ObjectReplacementCharacter), base);
    }

    void applyImageSize(QTextImageFormat& img, const AttributeMap& attrs)
    {
        std::optional<double> width = parseLength(attrs.value(QLatin1String("width")));
        std::optional<double> height = parseLength(attrs.value(QLatin1String("height")));
        // flutter_quill_extensions: the "style" declaration wins.
        const QHash<QString, QString> css = parseCss(attrs.value(QLatin1String("style")).toString());
        if (auto w = parseLength(css.value(QStringLiteral("width"))))
            width = w;
        if (auto h = parseLength(css.value(QStringLiteral("height"))))
            height = h;
        if (width)
            img.setWidth(*width);
        if (height)
            img.setHeight(*height);
    }

    void registerImageResource(const QString& source)
    {
        // data: URLs are decoded here; QTextDocument loads file and qrc URLs
        // itself, remote URLs are fetched by the editor (M7).
        if (!source.startsWith(QLatin1String("data:")))
            return;
        const qsizetype comma = source.indexOf(QLatin1Char(','));
        if (comma < 0)
            return;
        const QString meta = source.mid(5, comma - 5);
        const QByteArray payload = source.mid(comma + 1).toLatin1();
        const QByteArray bytes = meta.endsWith(QLatin1String(";base64"))
                                     ? QByteArray::fromBase64(payload)
                                     : QByteArray::fromPercentEncoding(payload);
        QImage image;
        if (image.loadFromData(bytes))
            m_doc->addResource(QTextDocument::ImageResource, QUrl(source), image);
    }

    QTextDocument* m_doc;
    const LoadOptions& m_options;
    StyleOptions m_style;
    QTextCursor m_cursor;
};

AttributeMap lineAttributesOf(const QTextBlock& block)
{
    return block.blockFormat().property(prop::BlockAttributes).toJsonObject();
}

} // namespace

StyleOptions DeltaDocument::effectiveStyle(const QTextDocument* doc, const StyleOptions& style)
{
    StyleOptions s = style;
    if (s.baseFont.family().isEmpty() && s.baseFont.families().isEmpty())
        s.baseFont = doc->defaultFont();
    return s;
}

LoadResult DeltaDocument::load(QTextDocument* doc, const Delta& deltaIn, const LoadOptions& options)
{
    LoadResult result;
    if (!deltaIn.isDocument()) {
        result.error = QStringLiteral("not a document: only insert operations are allowed");
        return result;
    }
    Delta delta = deltaIn;
    const bool endsWithNewline = !delta.ops().isEmpty() && delta.ops().back().isTextInsert()
                                 && delta.ops().back().text().endsWith(QLatin1Char('\n'));
    if (!endsWithNewline) {
        if (!delta.isEmpty())
            result.warnings.append(QStringLiteral("document does not end with \"\\n\"; one was appended"));
        delta.insert(QStringLiteral("\n"));
    }

    const StyleOptions style = effectiveStyle(doc, options.style);
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    doc->setIndentWidth(style.indentWidth);
    if (!options.baseUrl.isEmpty())
        doc->setBaseUrl(options.baseUrl);

    Loader(doc, options, style).run(delta, result);

    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
    result.ok = true;
    return result;
}

void DeltaDocument::regroupLists(const QTextBlock& firstIn, const QTextBlock& lastIn,
                                 const StyleOptions& style)
{
    if (!firstIn.isValid() || !lastIn.isValid())
        return;
    const auto isListLine = [](const QTextBlock& b) {
        return !formats::listGroup(lineAttributesOf(b)).isEmpty();
    };
    // Extend to the whole run of list lines, since numbering depends on it.
    QTextBlock first = firstIn;
    while (first.previous().isValid() && isListLine(first.previous()))
        first = first.previous();
    QTextBlock last = lastIn;
    while (last.next().isValid() && isListLine(last.next()))
        last = last.next();

    struct Level
    {
        QString group;
        QTextList* list = nullptr;
    };
    std::vector<Level> stack;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        const AttributeMap line = lineAttributesOf(b);
        const QString group = formats::listGroup(line);
        QTextList* current = b.textList();
        if (group.isEmpty()) {
            stack.clear();
            if (current)
                current->remove(b);
        } else {
            const int level = formats::indentLevel(line);
            stack.resize(size_t(level) + 1);
            Level& slot = stack[size_t(level)];
            const QTextListFormat wanted = formats::listFormat(line, style);
            if (slot.list && slot.group == group && slot.list->format().style() == wanted.style()
                && slot.list->format().indent() == wanted.indent()) {
                if (current != slot.list)
                    slot.list->add(b);
            } else {
                QTextCursor c(b);
                if (current)
                    current->remove(b);
                slot.list = c.createList(wanted);
                slot.group = group;
            }
        }
        if (b == last)
            break;
    }
}

} // namespace quill
