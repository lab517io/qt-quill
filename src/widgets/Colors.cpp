#include "quill/Colors.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace quill::colors {
namespace {

// flutter_quill's stringToColor() names (Flutter Material colour constants).
const QHash<QString, QRgb>& flutterNames()
{
    static const QHash<QString, QRgb> names{
        {QStringLiteral("transparent"), 0x00000000u},
        {QStringLiteral("black"), 0xFF000000u},
        {QStringLiteral("black12"), 0x1F000000u},
        {QStringLiteral("black26"), 0x42000000u},
        {QStringLiteral("black38"), 0x61000000u},
        {QStringLiteral("black45"), 0x73000000u},
        {QStringLiteral("black54"), 0x8A000000u},
        {QStringLiteral("black87"), 0xDD000000u},
        {QStringLiteral("white"), 0xFFFFFFFFu},
        {QStringLiteral("white10"), 0x1AFFFFFFu},
        {QStringLiteral("white12"), 0x1FFFFFFFu},
        {QStringLiteral("white24"), 0x3DFFFFFFu},
        {QStringLiteral("white30"), 0x4DFFFFFFu},
        {QStringLiteral("white38"), 0x62FFFFFFu},
        {QStringLiteral("white54"), 0x8AFFFFFFu},
        {QStringLiteral("white60"), 0x99FFFFFFu},
        {QStringLiteral("white70"), 0xB3FFFFFFu},
        {QStringLiteral("red"), 0xFFF44336u},
        {QStringLiteral("redAccent"), 0xFFFF5252u},
        {QStringLiteral("amber"), 0xFFFFC107u},
        {QStringLiteral("amberAccent"), 0xFFFFD740u},
        {QStringLiteral("yellow"), 0xFFFFEB3Bu},
        {QStringLiteral("yellowAccent"), 0xFFFFFF00u},
        {QStringLiteral("teal"), 0xFF009688u},
        {QStringLiteral("tealAccent"), 0xFF64FFDAu},
        {QStringLiteral("purple"), 0xFF9C27B0u},
        {QStringLiteral("purpleAccent"), 0xFFE040FBu},
        {QStringLiteral("pink"), 0xFFE91E63u},
        {QStringLiteral("pinkAccent"), 0xFFFF4081u},
        {QStringLiteral("orange"), 0xFFFF9800u},
        {QStringLiteral("orangeAccent"), 0xFFFFAB40u},
        {QStringLiteral("deepOrange"), 0xFFFF5722u},
        {QStringLiteral("deepOrangeAccent"), 0xFFFF6E40u},
        {QStringLiteral("indigo"), 0xFF3F51B5u},
        {QStringLiteral("indigoAccent"), 0xFF536DFEu},
        {QStringLiteral("lime"), 0xFFCDDC39u},
        {QStringLiteral("limeAccent"), 0xFFEEFF41u},
        {QStringLiteral("grey"), 0xFF9E9E9Eu},
        {QStringLiteral("blueGrey"), 0xFF607D8Bu},
        {QStringLiteral("green"), 0xFF4CAF50u},
        {QStringLiteral("greenAccent"), 0xFF69F0AEu},
        {QStringLiteral("lightGreen"), 0xFF8BC34Au},
        {QStringLiteral("lightGreenAccent"), 0xFFB2FF59u},
        {QStringLiteral("blue"), 0xFF2196F3u},
        {QStringLiteral("blueAccent"), 0xFF448AFFu},
        {QStringLiteral("lightBlue"), 0xFF03A9F4u},
        {QStringLiteral("lightBlueAccent"), 0xFF40C4FFu},
        {QStringLiteral("cyan"), 0xFF00BCD4u},
        {QStringLiteral("cyanAccent"), 0xFF18FFFFu},
        {QStringLiteral("brown"), 0xFF795548u},
    };
    return names;
}

std::optional<QColor> parseHex(const QString& hex, ColorDialect dialect)
{
    bool ok = false;
    const uint v = hex.toUInt(&ok, 16);
    if (!ok)
        return std::nullopt;
    switch (hex.size()) {
    case 3: {
        const int r = int((v >> 8) & 0xF), g = int((v >> 4) & 0xF), b = int(v & 0xF);
        return QColor(r * 17, g * 17, b * 17);
    }
    case 6:
        return QColor::fromRgb(QRgb(0xFF000000u | v));
    case 8:
        if (dialect == ColorDialect::Flutter)
            return QColor::fromRgba(QRgb(v)); // AARRGGBB
        return QColor::fromRgba(QRgb((v >> 8) | ((v & 0xFFu) << 24))); // RRGGBBAA
    default:
        return std::nullopt;
    }
}

// Parses one rgb()/hsl() component: "128", "50%", "0.5".
std::optional<double> component(QString s, double percentScale)
{
    s = s.trimmed();
    bool ok = false;
    if (s.endsWith(QLatin1Char('%'))) {
        const double v = s.chopped(1).toDouble(&ok);
        return ok ? std::optional<double>(v / 100.0 * percentScale) : std::nullopt;
    }
    const double v = s.toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

std::optional<QColor> parseFunction(const QString& value)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*(rgba?|hsla?)\s*\(([^)]*)\)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(value);
    if (!m.hasMatch())
        return std::nullopt;
    const QString fn = m.captured(1).toLower();
    // Accept both "r, g, b, a" and the CSS4 "r g b / a" syntax.
    QString args = m.captured(2);
    QStringList parts;
    if (args.contains(QLatin1Char(','))) {
        parts = args.split(QLatin1Char(','));
    } else {
        args.replace(QLatin1Char('/'), QLatin1Char(' '));
        parts = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    if (parts.size() < 3 || parts.size() > 4)
        return std::nullopt;
    double alpha = 1.0;
    if (parts.size() == 4) {
        const auto a = component(parts.at(3), 1.0);
        if (!a)
            return std::nullopt;
        alpha = std::clamp(*a, 0.0, 1.0);
    }
    if (fn.startsWith(QLatin1String("rgb"))) {
        double rgb[3];
        for (int i = 0; i < 3; ++i) {
            const auto c = component(parts.at(i), 255.0);
            if (!c)
                return std::nullopt;
            rgb[i] = std::clamp(*c, 0.0, 255.0);
        }
        QColor color = QColor::fromRgbF(float(rgb[0] / 255.0), float(rgb[1] / 255.0),
                                        float(rgb[2] / 255.0), float(alpha));
        return color;
    }
    const auto h = component(QString(parts.at(0)).remove(QStringLiteral("deg")), 360.0);
    const auto s = component(parts.at(1), 1.0);
    const auto l = component(parts.at(2), 1.0);
    if (!h || !s || !l)
        return std::nullopt;
    double hue = std::fmod(*h, 360.0);
    if (hue < 0)
        hue += 360.0;
    return QColor::fromHslF(float(hue / 360.0), float(std::clamp(*s, 0.0, 1.0)),
                            float(std::clamp(*l, 0.0, 1.0)), float(alpha));
}

std::optional<QColor> parseName(const QString& name, ColorDialect dialect)
{
    const auto flutter = [&]() -> std::optional<QColor> {
        const auto it = flutterNames().constFind(name);
        if (it == flutterNames().constEnd())
            return std::nullopt;
        return QColor::fromRgba(it.value());
    };
    const auto css = [&]() -> std::optional<QColor> {
        if (!QColor::isValidColorName(name))
            return std::nullopt;
        return QColor::fromString(name);
    };
    if (dialect == ColorDialect::Flutter) {
        if (auto c = flutter())
            return c;
        return css();
    }
    if (auto c = css())
        return c;
    return flutter();
}

} // namespace

std::optional<QColor> parse(const QString& valueIn, ColorDialect dialect)
{
    const QString value = valueIn.trimmed();
    if (value.isEmpty())
        return std::nullopt;
    if (value.startsWith(QLatin1Char('#')))
        return parseHex(value.mid(1), dialect);
    if (auto c = parseFunction(value))
        return c;
    if (value.compare(QLatin1String("inherit"), Qt::CaseInsensitive) == 0
        || value.compare(QLatin1String("initial"), Qt::CaseInsensitive) == 0
        || value.compare(QLatin1String("currentcolor"), Qt::CaseInsensitive) == 0)
        return std::nullopt;
    return parseName(value, dialect);
}

QString format(const QColor& color, const QString& original, ColorDialect dialect)
{
    if (!original.isEmpty()) {
        const auto parsed = parse(original, dialect);
        if (parsed && parsed->rgba() == color.rgba())
            return original;
    }
    if (dialect == ColorDialect::Flutter)
        return QStringLiteral("#%1").arg(color.rgba(), 8, 16, QLatin1Char('0')).toUpper();
    if (color.alpha() == 255)
        return color.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'g', 3));
}

} // namespace quill::colors
