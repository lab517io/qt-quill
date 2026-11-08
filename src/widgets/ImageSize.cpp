#include "ImageSize.h"

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

} // namespace quill::detail
