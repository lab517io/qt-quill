#include "ImageSize.h"

#include <QList>

#include <algorithm>
#include <cmath>

namespace quill::detail {

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

std::pair<std::optional<double>, std::optional<double>> imageSize(const AttributeMap& attrs)
{
    std::optional<double> width = parseLength(attrs.value(QLatin1String("width")));
    std::optional<double> height = parseLength(attrs.value(QLatin1String("height")));
    const QHash<QString, QString> css = parseCss(attrs.value(QLatin1String("style")).toString());
    if (auto w = parseLength(css.value(QStringLiteral("width"))))
        width = w;
    if (auto h = parseLength(css.value(QStringLiteral("height"))))
        height = h;
    return {width, height};
}

QString removeCssProperties(const QString& css, const QStringList& properties)
{
    QStringList kept;
    for (const QString& decl : css.split(QLatin1Char(';'))) {
        const QString name = decl.section(QLatin1Char(':'), 0, 0).trimmed();
        if (decl.trimmed().isEmpty() || properties.contains(name))
            continue;
        kept << decl.trimmed();
    }
    return kept.isEmpty() ? QString() : kept.join(QStringLiteral("; ")) + QLatin1Char(';');
}

QString formatLength(double value)
{
    return QString::number(std::round(value * 100.0) / 100.0, 'g', 10);
}

namespace {

/// Dart's double.toString() for the whole and two-decimal numbers written here.
QString dartDouble(double v)
{
    if (v == std::floor(v) && std::abs(v) < 1e15)
        return QString::number(qint64(v)) + QStringLiteral(".0");
    return QString::number(v, 'g', 17);
}

/// The declarations of a style string as flutter_quill_extensions reads
/// them: split at ";", key before the first ":", both trimmed; insertion
/// order kept, a repeated key keeps its first place.
QList<std::pair<QString, QString>> declarations(const QString& css)
{
    QList<std::pair<QString, QString>> out;
    for (const QString& pair : css.split(QLatin1Char(';'))) {
        const qsizetype colon = pair.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = pair.left(colon).trimmed();
        const QString value = pair.mid(colon + 1).trimmed();
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& d) { return d.first == key; });
        if (it != out.end())
            it->second = value;
        else
            out.append({key, value});
    }
    return out;
}

QString writeDeclarations(const QList<std::pair<QString, QString>>& decls)
{
    QString out;
    for (const auto& [key, value] : decls)
        out += key + QStringLiteral(": ") + value + QStringLiteral("; ");
    return out;
}

const QString kWidth = QStringLiteral("width");
const QString kHeight = QStringLiteral("height");
const QString kStyle = QStringLiteral("style");

} // namespace

QString replaceStyleStringWithSize(const QString& css, double width, double height)
{
    auto decls = declarations(css);
    for (const auto& size : {std::pair{kWidth, width}, std::pair{kHeight, height}}) {
        const QString& key = size.first;
        auto it = std::find_if(decls.begin(), decls.end(), [&](const auto& d) { return d.first == key; });
        if (it != decls.end())
            it->second = dartDouble(size.second);
        else
            decls.append({key, dartDouble(size.second)});
    }
    return writeDeclarations(decls);
}

AttributeMap resizedImageAttributes(const AttributeMap& current, const std::optional<QSizeF>& size,
                                    SizeSpelling spelling)
{
    const QString style = current.value(kStyle).toString();
    const auto decls = declarations(style);
    const bool styleHasSize =
        std::any_of(decls.begin(), decls.end(), [](const auto& d) { return d.first == kWidth || d.first == kHeight; });
    const bool hasAttrs = current.contains(kWidth) || current.contains(kHeight);
    const QJsonValue null(QJsonValue::Null);
    AttributeMap change;

    if (!size) {
        // Natural size: no size anywhere, everything else stays.
        if (styleHasSize) {
            QList<std::pair<QString, QString>> kept;
            for (const auto& d : decls) {
                if (d.first != kWidth && d.first != kHeight)
                    kept.append(d);
            }
            change.insert(kStyle, kept.isEmpty() ? null : QJsonValue(writeDeclarations(kept)));
        }
        if (current.contains(kWidth))
            change.insert(kWidth, null);
        if (current.contains(kHeight))
            change.insert(kHeight, null);
        return change;
    }

    const double w = std::round(size->width());
    const double h = std::round(size->height());
    if (spelling == SizeSpelling::Auto)
        spelling = hasAttrs && !styleHasSize ? SizeSpelling::QuillJs : SizeSpelling::FlutterQuill;
    if (spelling == SizeSpelling::FlutterQuill) {
        change.insert(kStyle, replaceStyleStringWithSize(style, w, h));
        return change;
    }
    // Quill.js: string attributes as its image format writes them; a
    // number stays a number.
    const auto value = [&](const QString& key, double v) {
        return current.value(key).isDouble() ? QJsonValue(v) : QJsonValue(QString::number(qint64(v)));
    };
    change.insert(kWidth, value(kWidth, w));
    change.insert(kHeight, value(kHeight, h));
    if (styleHasSize) {
        // The style would win over the attributes; take the size out of it.
        QList<std::pair<QString, QString>> kept;
        for (const auto& d : decls) {
            if (d.first != kWidth && d.first != kHeight)
                kept.append(d);
        }
        change.insert(kStyle, kept.isEmpty() ? null : QJsonValue(writeDeclarations(kept)));
    }
    return change;
}

} // namespace quill::detail
