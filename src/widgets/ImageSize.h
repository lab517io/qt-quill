#pragma once

// Image size attributes, shared by the loader and the exporter (private header).

#include "quill/Attributes.h"

#include <QHash>
#include <QJsonValue>
#include <QSizeF>
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

/// Which attributes an image resize writes.
enum class SizeSpelling {
    Auto,         ///< the image's own: width/height when it has them and no size in "style", else "style"
    FlutterQuill, ///< "style": "width: 240.0; height: 135.0; " (flutter_quill_extensions)
    QuillJs,      ///< "width": "240", "height": "135" (Quill.js image format)
};

/// flutter_quill_extensions' replaceStyleStringWithSize: the declarations
/// of `css` in order (later duplicates replace earlier values), width and
/// height set (Dart double.toString: "240.0"), written as "key: value; ".
QString replaceStyleStringWithSize(const QString& css, double width, double height);

/// The attribute change that gives an image with attributes `current` the
/// size `size` (whole pixels), or its natural size when `size` is empty
/// (size properties removed, other style declarations kept). Removed
/// attributes are null.
AttributeMap resizedImageAttributes(const AttributeMap& current, const std::optional<QSizeF>& size,
                                    SizeSpelling spelling);

} // namespace quill::detail
