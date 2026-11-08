// C++ port of slab/delta 5.1 (BSD 3-Clause, Copyright (c) 2022, Slab, Inc.).
// See THIRD_PARTY_NOTICES.md.

#include "quill/Attributes.h"

namespace quill::attributes {

AttributeMap compose(const AttributeMap& a, const AttributeMap& b, bool keepNull)
{
    AttributeMap result;
    for (auto it = b.begin(); it != b.end(); ++it) {
        if (keepNull || !(it.value().isNull() || it.value().isUndefined()))
            result.insert(it.key(), it.value());
    }
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (!it.value().isUndefined() && !b.contains(it.key()))
            result.insert(it.key(), it.value());
    }
    return result;
}

AttributeMap diff(const AttributeMap& a, const AttributeMap& b)
{
    AttributeMap result;
    const auto visit = [&](const QString& key) {
        const QJsonValue av = a.value(key);
        const QJsonValue bv = b.value(key);
        if (av != bv)
            result.insert(key, bv.isUndefined() ? QJsonValue(QJsonValue::Null) : bv);
    };
    for (auto it = a.begin(); it != a.end(); ++it)
        visit(it.key());
    for (auto it = b.begin(); it != b.end(); ++it)
        visit(it.key());
    return result;
}

AttributeMap invert(const AttributeMap& attr, const AttributeMap& base)
{
    AttributeMap result;
    for (auto it = base.begin(); it != base.end(); ++it) {
        const QJsonValue av = attr.value(it.key());
        if (!av.isUndefined() && it.value() != av)
            result.insert(it.key(), it.value());
    }
    for (auto it = attr.begin(); it != attr.end(); ++it) {
        if (!base.contains(it.key()))
            result.insert(it.key(), QJsonValue(QJsonValue::Null));
    }
    return result;
}

AttributeMap transform(const AttributeMap& a, const AttributeMap& b, bool priority)
{
    if (a.isEmpty() || !priority)
        return b; // b simply overwrites a without priority
    AttributeMap result;
    for (auto it = b.begin(); it != b.end(); ++it) {
        if (!a.contains(it.key()))
            result.insert(it.key(), it.value()); // null is a valid value
    }
    return result;
}

} // namespace quill::attributes
