#pragma once

#include <QColor>
#include <QString>

#include <optional>

namespace quill {

/// How colour strings in Delta attributes are interpreted.
enum class ColorDialect {
    /// flutter_quill: 8-digit hex is #AARRGGBB, names are Flutter's Material
    /// colours ("red" is #F44336, "black54" is 54% black), rgba() alpha is 0..1.
    Flutter,
    /// Quill.js / CSS: 8-digit hex is #RRGGBBAA, names are CSS named colours.
    Css,
};

namespace colors {

/// Parses a colour attribute value. Supports #rgb, #rrggbb, 8-digit hex (per
/// dialect), names (per dialect, then the other dialect), rgb(), rgba(),
/// hsl() and hsla() with integers, decimals or percentages. Returns nullopt
/// for values with no colour meaning, such as "inherit" or garbage.
std::optional<QColor> parse(const QString& value, ColorDialect dialect = ColorDialect::Flutter);

/// Formats a colour for writing back into a Delta. Returns `original` when it
/// still parses to the same colour, so untouched values round-trip exactly.
/// Otherwise flutter_quill style "#AARRGGBB" (uppercase) or CSS "#rrggbb" /
/// "rgba(r, g, b, a)".
QString format(const QColor& color, const QString& original = QString(),
               ColorDialect dialect = ColorDialect::Flutter);

} // namespace colors
} // namespace quill
