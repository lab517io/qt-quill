#pragma once

#include <QJsonObject>

namespace quill {

/// Attribute map of a Delta operation, e.g. {"bold": true, "header": 1}.
///
/// A JSON null value means "remove this attribute" (only meaningful in
/// retain operations). An empty map is equivalent to "no attributes"; the
/// JavaScript distinction between `undefined` and `{}` is not kept.
using AttributeMap = QJsonObject;

/// Attribute algebra with the semantics of slab/delta's AttributeMap.
namespace attributes {

/// Applies `b` on top of `a`. Null values in `b` are dropped unless
/// `keepNull` is true, in which case they are kept as removal markers.
AttributeMap compose(const AttributeMap& a, const AttributeMap& b, bool keepNull = false);

/// Returns the change that turns `a` into `b`; removed keys map to null.
AttributeMap diff(const AttributeMap& a, const AttributeMap& b);

/// Returns the attributes that undo `attr` applied on top of `base`.
AttributeMap invert(const AttributeMap& attr, const AttributeMap& base);

/// Transforms `b` against the concurrent change `a`. With `priority`, `a`
/// is considered to have happened first and wins conflicting keys.
AttributeMap transform(const AttributeMap& a, const AttributeMap& b, bool priority = false);

} // namespace attributes
} // namespace quill
