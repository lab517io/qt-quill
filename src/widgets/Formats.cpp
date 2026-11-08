#include "quill/Formats.h"

#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>

namespace quill {

namespace {

QString str(const char* s) { return QString::fromLatin1(s); }

std::optional<int> intValue(const QJsonValue& v)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (std::floor(d) == d)
            return int(d);
        return std::nullopt;
    }
    if (v.isString()) { // flutter_quill occasionally stores numbers as strings
        bool ok = false;
        const int i = v.toString().toInt(&ok);
        if (ok)
            return i;
    }
    return std::nullopt;
}

std::optional<double> doubleValue(const QJsonValue& v)
{
    if (v.isDouble())
        return v.toDouble();
    if (v.isString()) {
        bool ok = false;
        const double d = v.toString().toDouble(&ok);
        if (ok)
            return d;
    }
    return std::nullopt;
}

bool isTrue(const AttributeMap& attrs, const char* key)
{
    const QJsonValue v = attrs.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : (!v.isNull() && !v.isUndefined());
}

std::optional<int> headerLevel(const AttributeMap& lineAttrs)
{
    const auto level = intValue(lineAttrs.value(QLatin1String("header")));
    if (level && *level >= 1 && *level <= 6)
        return level;
    return std::nullopt;
}

/// Font size in Delta units requested by inline "size", or nullopt.
std::optional<double> sizeAttribute(const QJsonValue& v, const StyleOptions& style)
{
    if (v.isString()) {
        const QString s = v.toString();
        if (s == QLatin1String("small"))
            return style.sizeSmall;
        if (s == QLatin1String("large"))
            return style.sizeLarge;
        if (s == QLatin1String("huge"))
            return style.sizeHuge;
        if (s == QLatin1String("normal"))
            return style.referenceSize;
    }
    const auto d = doubleValue(v);
    if (d && *d > 0)
        return d;
    return std::nullopt; // includes flutter_quill's "0" = clear
}

QStringList genericFamilies(const QString& name, QFont::StyleHint* hint)
{
    if (name == QLatin1String("serif")) {
        *hint = QFont::Serif;
        return {str("serif"), str("Noto Serif"), str("DejaVu Serif"), str("Times New Roman"),
                str("Times")};
    }
    if (name == QLatin1String("sans-serif")) {
        *hint = QFont::SansSerif;
        return {str("sans-serif"), str("Noto Sans"), str("DejaVu Sans"), str("Arial"),
                str("Helvetica")};
    }
    if (name == QLatin1String("monospace")) {
        *hint = QFont::Monospace;
        return {};
    }
    *hint = QFont::AnyStyle;
    return {name};
}

void applyMonospace(QTextCharFormat& f, const StyleOptions& style)
{
    QStringList families = style.monospaceFamilies;
    families.append(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
    f.setFontFamilies(families);
    f.setFontFixedPitch(true);
    f.setFontStyleHint(QFont::Monospace);
}

} // namespace

double StyleOptions::basePointSize() const
{
    const double pt = baseFont.pointSizeF();
    if (pt > 0)
        return pt;
    if (baseFont.pixelSize() > 0)
        return baseFont.pixelSize() * 0.75;
    return 10.0;
}

double StyleOptions::pointSizeFor(double deltaSize) const
{
    return basePointSize() * deltaSize / referenceSize;
}

namespace formats {

const QSet<QString>& inlineKeys()
{
    static const QSet<QString> keys{str("bold"),   str("italic"), str("underline"), str("strike"),
                                    str("code"),   str("script"), str("link"),      str("color"),
                                    str("background"), str("font"), str("size"),    str("small")};
    return keys;
}

const QSet<QString>& lineKeys()
{
    static const QSet<QString> keys{str("header"),     str("list"),       str("indent"),
                                    str("align"),      str("direction"),  str("blockquote"),
                                    str("code-block"), str("line-height")};
    return keys;
}

const QSet<QString>& passiveKeys()
{
    static const QSet<QString> keys{str("width"), str("height"), str("style"), str("token"),
                                    str("placeholder")};
    return keys;
}

QTextCharFormat charFormat(const AttributeMap& a, const AttributeMap& line, const StyleOptions& style)
{
    QTextCharFormat f;
    f.setProperty(prop::InlineAttributes, a);

    const bool codeBlock = line.contains(QLatin1String("code-block"))
                           && !line.value(QLatin1String("code-block")).isNull()
                           && line.value(QLatin1String("code-block")) != QJsonValue(false);
    const auto header = headerLevel(line);

    // Size precedence: inline size > small > inline code > header > code block.
    std::optional<double> size;
    if (auto s = sizeAttribute(a.value(QLatin1String("size")), style))
        size = s;
    else if (isTrue(a, "small"))
        size = style.smallAttributeSize;
    else if (header)
        size = style.headerSizes[*header - 1];
    else if (isTrue(a, "code"))
        size = style.inlineCodeSize;
    else if (codeBlock)
        size = style.codeBlockSize;
    if (size)
        f.setFontPointSize(style.pointSizeFor(*size));

    if (header || isTrue(a, "bold"))
        f.setFontWeight(QFont::Bold);
    if (isTrue(a, "italic"))
        f.setFontItalic(true);
    if (isTrue(a, "underline"))
        f.setFontUnderline(true);
    if (isTrue(a, "strike"))
        f.setFontStrikeOut(true);

    const QString script = a.value(QLatin1String("script")).toString();
    if (script == QLatin1String("sub"))
        f.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    else if (script == QLatin1String("super"))
        f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);

    if (codeBlock) {
        applyMonospace(f, style);
        f.setForeground(style.codeBlockForeground);
    } else if (isTrue(a, "code")) {
        applyMonospace(f, style);
        f.setBackground(style.inlineCodeBackground);
    }

    const QJsonValue font = a.value(QLatin1String("font"));
    if (font.isString() && !font.toString().isEmpty() && !codeBlock) {
        QFont::StyleHint hint = QFont::AnyStyle;
        const QStringList families = genericFamilies(font.toString(), &hint);
        if (hint == QFont::Monospace)
            applyMonospace(f, style);
        else {
            f.setFontFamilies(families);
            if (hint != QFont::AnyStyle)
                f.setFontStyleHint(hint);
        }
    }

    if (line.contains(QLatin1String("blockquote")) && isTrue(line, "blockquote"))
        f.setForeground(style.blockquoteForeground);

    const QJsonValue link = a.value(QLatin1String("link"));
    if (link.isString() && !link.toString().isEmpty()) {
        f.setAnchor(true);
        f.setAnchorHref(link.toString());
        f.setFontUnderline(true);
        f.setForeground(style.linkColor);
    }

    if (auto c = colors::parse(a.value(QLatin1String("color")).toString(), style.colorDialect))
        f.setForeground(*c);
    if (auto c = colors::parse(a.value(QLatin1String("background")).toString(), style.colorDialect))
        f.setBackground(*c);

    return f;
}

QTextBlockFormat blockFormat(const AttributeMap& line, const StyleOptions& style)
{
    QTextBlockFormat f;
    f.setProperty(prop::BlockAttributes, line);

    BlockKind kind = BlockKind::Paragraph;
    if (auto level = headerLevel(line)) {
        f.setHeadingLevel(*level);
        kind = BlockKind::Header;
    }
    const bool list = !listGroup(line).isEmpty();
    const bool codeBlock = line.contains(QLatin1String("code-block")) && isTrue(line, "code-block");
    const bool quote = line.contains(QLatin1String("blockquote")) && isTrue(line, "blockquote");
    if (codeBlock)
        kind = BlockKind::CodeBlock;
    else if (quote)
        kind = BlockKind::Blockquote;
    else if (list && kind != BlockKind::Header)
        kind = BlockKind::ListItem;
    f.setProperty(prop::BlockKind, int(kind));

    const int indent = indentLevel(line);
    if (!list && indent > 0)
        f.setIndent(indent);
    if (quote)
        f.setLeftMargin(style.blockquoteMargin);
    if (codeBlock) {
        f.setBackground(style.codeBlockBackground);
        f.setNonBreakableLines(false);
    }

    if (list) {
        const QString value = line.value(QLatin1String("list")).toString();
        if (value == QLatin1String("checked"))
            f.setMarker(QTextBlockFormat::MarkerType::Checked);
        else if (value == QLatin1String("unchecked"))
            f.setMarker(QTextBlockFormat::MarkerType::Unchecked);
    }

    const QString align = line.value(QLatin1String("align")).toString();
    if (align == QLatin1String("left"))
        f.setAlignment(Qt::AlignLeft | Qt::AlignAbsolute);
    else if (align == QLatin1String("center"))
        f.setAlignment(Qt::AlignHCenter);
    else if (align == QLatin1String("right"))
        f.setAlignment(Qt::AlignRight | Qt::AlignAbsolute);
    else if (align == QLatin1String("justify"))
        f.setAlignment(Qt::AlignJustify);

    if (line.value(QLatin1String("direction")).toString() == QLatin1String("rtl"))
        f.setLayoutDirection(Qt::RightToLeft);

    if (auto lh = doubleValue(line.value(QLatin1String("line-height"))); lh && *lh > 0)
        f.setLineHeight(*lh * 100.0, QTextBlockFormat::ProportionalHeight);

    return f;
}

QString listGroup(const AttributeMap& line)
{
    const QString value = line.value(QLatin1String("list")).toString();
    if (value == QLatin1String("bullet") || value == QLatin1String("ordered"))
        return value;
    if (value == QLatin1String("checked") || value == QLatin1String("unchecked"))
        return str("check");
    return {};
}

int indentLevel(const AttributeMap& line)
{
    const auto v = intValue(line.value(QLatin1String("indent")));
    return v && *v > 0 ? *v : 0;
}

QTextListFormat listFormat(const AttributeMap& line, const StyleOptions& style)
{
    const QString group = listGroup(line);
    if (group.isEmpty())
        return {};
    const int level = indentLevel(line);
    QTextListFormat f;
    if (group == QLatin1String("ordered")) {
        // flutter_quill: 1. / a. / i. cycling with the indent level.
        static const QTextListFormat::Style styles[] = {QTextListFormat::ListDecimal,
                                                        QTextListFormat::ListLowerAlpha,
                                                        QTextListFormat::ListLowerRoman};
        f.setStyle(styles[level % 3]);
    } else {
        static const QTextListFormat::Style styles[] = {
            QTextListFormat::ListDisc, QTextListFormat::ListCircle, QTextListFormat::ListSquare};
        f.setStyle(styles[level % 3]);
    }
    f.setIndent(level + 1);
    Q_UNUSED(style);
    return f;
}

QStringList validate(const AttributeMap& attrs, bool line, const StyleOptions& style)
{
    QStringList problems;
    const auto complain = [&](const QString& key) {
        problems.append(QStringLiteral("%1: unsupported value %2")
                            .arg(key, QString::fromUtf8(QJsonDocument(QJsonArray{attrs.value(key)})
                                                            .toJson(QJsonDocument::Compact))
                                          .mid(1)
                                          .chopped(1)));
    };
    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
        const QString& key = it.key();
        const QJsonValue v = it.value();
        if (v.isNull())
            continue; // removal marker, valid anywhere
        if (key == QLatin1String("bold") || key == QLatin1String("italic")
            || key == QLatin1String("underline") || key == QLatin1String("strike")
            || key == QLatin1String("code") || key == QLatin1String("small")
            || key == QLatin1String("blockquote")) {
            if (!v.isBool())
                complain(key);
        } else if (key == QLatin1String("code-block")) {
            if (!v.isBool() && !v.isString()) // flutter_quill: true; Quill 2: language
                complain(key);
        } else if (key == QLatin1String("script")) {
            if (v.toString() != QLatin1String("sub") && v.toString() != QLatin1String("super"))
                complain(key);
        } else if (key == QLatin1String("link") || key == QLatin1String("font")) {
            if (!v.isString())
                complain(key);
        } else if (key == QLatin1String("color") || key == QLatin1String("background")) {
            if (!colors::parse(v.toString(), style.colorDialect)
                && v.toString() != QLatin1String("inherit"))
                complain(key);
        } else if (key == QLatin1String("size")) {
            if (!sizeAttribute(v, style) && v.toString() != QLatin1String("0"))
                complain(key);
        } else if (key == QLatin1String("header")) {
            if (!headerLevel(attrs))
                complain(key);
        } else if (key == QLatin1String("list")) {
            if (listGroup(attrs).isEmpty())
                complain(key);
        } else if (key == QLatin1String("indent")) {
            const auto i = intValue(v);
            if (!i || *i < 0)
                complain(key);
        } else if (key == QLatin1String("align")) {
            static const QSet<QString> ok{str("left"), str("center"), str("right"), str("justify")};
            if (!ok.contains(v.toString()))
                complain(key);
        } else if (key == QLatin1String("direction")) {
            if (v.toString() != QLatin1String("rtl"))
                complain(key);
        } else if (key == QLatin1String("line-height")) {
            const auto d = doubleValue(v);
            if (!d || *d <= 0)
                complain(key);
        }
    }
    Q_UNUSED(line);
    return problems;
}

} // namespace formats
} // namespace quill
