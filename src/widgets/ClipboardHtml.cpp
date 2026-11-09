#include "ClipboardHtml.h"

#include "quill/Formats.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace quill::detail {

namespace {

QString str(const char* s)
{
    return QString::fromLatin1(s);
}

// Word list marker token: U+E000, 'o' (ordered) or 'b' (bullet), level digit, U+E001.
constexpr char16_t kTokenStart = 0xE000;
constexpr char16_t kTokenEnd = 0xE001;

QString wordLists(const QString& html)
{
    if (!html.contains(QLatin1String("mso-list")))
        return html;
    // <p ... style='...mso-list:l0 level2 lfo1...'> ... <![if !supportLists]>MARKER<![endif]> ... </p>
    static const QRegularExpression para(QStringLiteral("(<p\\b[^>]*mso-list:\\s*l\\d+\\s+level(\\d+)[^>]*>)(.*?)</p>"),
                                         QRegularExpression::DotMatchesEverythingOption
                                             | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression marker(QStringLiteral("<!\\[if !supportLists\\]>(.*?)<!\\[endif\\]>"),
                                           QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression ordered(QStringLiteral("^\\(?[0-9a-zA-Z]{1,6}[.)]$"));
    QString out;
    qsizetype last = 0;
    auto it = para.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QString body = m.captured(3);
        const QRegularExpressionMatch mk = marker.match(body);
        QString text = mk.hasMatch() ? mk.captured(1) : QString();
        text.remove(tags);
        text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
        text = text.trimmed();
        const bool isOrdered = ordered.match(text).hasMatch();
        if (mk.hasMatch())
            body.remove(mk.capturedStart(), mk.capturedLength());
        const int level = std::clamp(m.captured(2).toInt(), 1, 9);
        const QString token = QString(QChar(kTokenStart)) + (isOrdered ? QLatin1Char('o') : QLatin1Char('b'))
                              + QString::number(level) + QChar(kTokenEnd);
        out += html.mid(last, m.capturedStart() - last) + m.captured(1) + token + body + QStringLiteral("</p>");
        last = m.capturedEnd();
    }
    out += html.mid(last);
    return out;
}

bool isMonospace(const QJsonValue& font)
{
    const QString f = font.toString().toLower();
    return f == QLatin1String("monospace") || f.contains(QLatin1String("courier")) || f.contains(QLatin1String("mono"))
           || f.contains(QLatin1String("consolas")) || f.contains(QLatin1String("menlo"));
}

} // namespace

QString normalizeClipboardHtml(const QString& htmlIn)
{
    QString html = htmlIn;
    // CF_HTML: "Version:0.9\r\nStartHTML:..." header before the markup.
    if (html.startsWith(QLatin1String("Version:")) && html.contains(QLatin1String("StartHTML:"))) {
        const qsizetype lt = html.indexOf(QLatin1Char('<'));
        if (lt >= 0)
            html = html.mid(lt);
    }
    static const QRegularExpression interchange(QStringLiteral("<br[^>]*Apple-interchange-newline[^>]*>"),
                                                QRegularExpression::CaseInsensitiveOption);
    html.remove(interchange);
    static const QRegularExpression strike(QStringLiteral("<(/?)(strike|del)\\b([^>]*)>"),
                                           QRegularExpression::CaseInsensitiveOption);
    html.replace(strike, QStringLiteral("<\\1s\\3>"));
    return wordLists(html);
}

void markImportedBlocks(QTextDocument* doc)
{
    QTextCursor c(doc);
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextBlockFormat f = b.blockFormat();
        AttributeMap line = f.property(prop::BlockAttributes).toJsonObject();
        if (f.nonBreakableLines())
            line.insert(str("code-block"), true);
        else if (f.intProperty(QTextFormat::BlockQuoteLevel) > 0)
            line.insert(str("blockquote"), true);
        else
            continue;
        QTextBlockFormat changed = f;
        changed.setProperty(prop::BlockAttributes, line);
        c.setPosition(b.position());
        c.setBlockFormat(changed);
    }
}

Delta cleanPastedDelta(const Delta& delta, const QStringList& ignored)
{
    Delta out;
    delta.eachLine([&](const Delta& lineIn, const AttributeMap& lineAttrsIn, qsizetype) {
        AttributeMap lineAttrs = lineAttrsIn;
        const bool code = lineAttrs.contains(str("code-block"));
        QList<Op> ops = lineIn.ops();
        // Word list token at the start of the line.
        if (!ops.isEmpty() && ops.first().isTextInsert() && ops.first().text().startsWith(QChar(kTokenStart))) {
            const QString t = ops.first().text();
            const qsizetype end = t.indexOf(QChar(kTokenEnd));
            if (end == 3) {
                lineAttrs.insert(str("list"), t.at(1) == QLatin1Char('o') ? str("ordered") : str("bullet"));
                const int level = t.at(2).digitValue() - 1;
                if (level > 0)
                    lineAttrs.insert(str("indent"), level);
                QString rest = t.mid(end + 1);
                while (rest.startsWith(QLatin1Char(' ')) || rest.startsWith(QChar(0xA0)))
                    rest.remove(0, 1);
                ops.first() = Op::insert(rest, ops.first().attributes());
            }
        }
        // Whitespace at a line end is collapsed by HTML; Qt keeps one space.
        if (!code && !ops.isEmpty() && ops.last().isTextInsert()) {
            QString t = ops.last().text();
            while (t.endsWith(QLatin1Char(' ')) || t.endsWith(QChar(0xA0)))
                t.chop(1);
            ops.last() = Op::insert(t, ops.last().attributes());
        }
        for (Op op : ops) {
            if (op.isTextInsert() && op.text().isEmpty())
                continue;
            AttributeMap attrs = op.attributes();
            if (attrs.contains(str("font")) && isMonospace(attrs.value(str("font")))) {
                attrs.remove(str("font"));
                if (!code)
                    attrs.insert(str("code"), true);
            }
            for (const QString& k : ignored)
                attrs.remove(k);
            op.setAttributes(attrs);
            out.push(op);
        }
        for (const QString& k : ignored)
            lineAttrs.remove(k);
        out.insert(QStringLiteral("\n"), lineAttrs);
        return true;
    });
    // A last line without "\n" (eachLine drops it) does not occur: exports end with "\n".
    return out;
}

} // namespace quill::detail
