#pragma once

// Image size attributes, shared by the loader and the exporter (private header).

#include "quill/Attributes.h"

#include <QHash>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <optional>

namespace quill::detail {

/// A bare number or a CSS length in px / em / rem (16 px per em), as
/// flutter_quill_extensions parses it. Percent and viewport units: nullopt.
std::optional<double> parseLength(const QJsonValue& v);

/// "width:200px; height:50px;" -> {width: 200px, height: 50px}, keys trimmed.
QHash<QString, QString> parseCss(const QString& css);

/// Width and height an image embed asks for; the "style" declaration wins
/// over the width/height attributes (flutter_quill_extensions).
std::pair<std::optional<double>, std::optional<double>> imageSize(const AttributeMap& attrs);

/// Returns `css` without the given properties, keeping the rest verbatim.
QString removeCssProperties(const QString& css, const QStringList& properties);

/// Formats a length the way flutter_quill writes it: "200", "12.5".
QString formatLength(double value);

} // namespace quill::detail
