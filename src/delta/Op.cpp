// C++ port of slab/delta 5.1 (BSD 3-Clause, Copyright (c) 2022, Slab, Inc.).
// See THIRD_PARTY_NOTICES.md.

#include "quill/Op.h"

#include <cmath>

namespace quill {

Op Op::insert(const QString& text, const AttributeMap& attributes)
{
    Op op;
    op.m_type = Type::Insert;
    op.m_count = 0;
    op.m_text = text;
    op.m_attributes = attributes;
    return op;
}

Op Op::insert(const QJsonObject& embed, const AttributeMap& attributes)
{
    Op op;
    op.m_type = Type::Insert;
    op.m_isEmbed = true;
    op.m_count = 0;
    op.m_embed = embed;
    op.m_attributes = attributes;
    return op;
}

Op Op::retain(qsizetype length, const AttributeMap& attributes)
{
    Op op;
    op.m_type = Type::Retain;
    op.m_count = length;
    op.m_attributes = attributes;
    return op;
}

Op Op::retain(const QJsonObject& embed, const AttributeMap& attributes)
{
    Op op;
    op.m_type = Type::Retain;
    op.m_isEmbed = true;
    op.m_count = 0;
    op.m_embed = embed;
    op.m_attributes = attributes;
    return op;
}

Op Op::remove(qsizetype length)
{
    Op op;
    op.m_type = Type::Delete;
    op.m_count = length;
    return op;
}

qsizetype Op::length() const
{
    if (m_isEmbed)
        return 1;
    if (m_type == Type::Insert)
        return m_text.size();
    return m_count;
}

QJsonObject Op::toJson() const
{
    QJsonObject json;
    switch (m_type) {
    case Type::Insert:
        json.insert(QStringLiteral("insert"), m_isEmbed ? QJsonValue(m_embed) : QJsonValue(m_text));
        break;
    case Type::Retain:
        json.insert(QStringLiteral("retain"), m_isEmbed ? QJsonValue(m_embed) : QJsonValue(qint64(m_count)));
        break;
    case Type::Delete:
        json.insert(QStringLiteral("delete"), qint64(m_count));
        break;
    }
    if (!m_attributes.isEmpty() && m_type != Type::Delete)
        json.insert(QStringLiteral("attributes"), m_attributes);
    return json;
}

namespace {

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool readCount(const QJsonValue& value, const char* key, qsizetype* out, QString* error)
{
    const double d = value.toDouble(-1);
    if (!value.isDouble() || d < 0 || std::floor(d) != d)
        return fail(error, QStringLiteral("\"%1\" must be a non-negative integer").arg(QLatin1String(key)));
    *out = qsizetype(d);
    return true;
}

} // namespace

std::optional<Op> Op::fromJson(const QJsonValue& value, QString* error)
{
    if (!value.isObject()) {
        fail(error, QStringLiteral("operation must be an object"));
        return std::nullopt;
    }
    const QJsonObject json = value.toObject();
    const QJsonValue attrs = json.value(QStringLiteral("attributes"));
    if (!attrs.isUndefined() && !attrs.isNull() && !attrs.isObject()) {
        fail(error, QStringLiteral("\"attributes\" must be an object"));
        return std::nullopt;
    }
    const AttributeMap attributes = attrs.toObject();

    const QJsonValue ins = json.value(QStringLiteral("insert"));
    const QJsonValue ret = json.value(QStringLiteral("retain"));
    const QJsonValue del = json.value(QStringLiteral("delete"));
    const int kinds = int(!ins.isUndefined()) + int(!ret.isUndefined()) + int(!del.isUndefined());
    if (kinds != 1) {
        fail(error, QStringLiteral("operation must have exactly one of insert, retain, delete"));
        return std::nullopt;
    }
    if (!ins.isUndefined()) {
        if (ins.isString())
            return insert(ins.toString(), attributes);
        if (ins.isObject())
            return insert(ins.toObject(), attributes);
        fail(error, QStringLiteral("\"insert\" must be a string or an object"));
        return std::nullopt;
    }
    if (!ret.isUndefined()) {
        if (ret.isObject())
            return retain(ret.toObject(), attributes);
        qsizetype n = 0;
        if (!readCount(ret, "retain", &n, error))
            return std::nullopt;
        return retain(n, attributes);
    }
    qsizetype n = 0;
    if (!readCount(del, "delete", &n, error))
        return std::nullopt;
    return remove(n);
}

bool Op::operator==(const Op& other) const
{
    return m_type == other.m_type && m_isEmbed == other.m_isEmbed && m_count == other.m_count && m_text == other.m_text
           && m_embed == other.m_embed && m_attributes == other.m_attributes;
}

} // namespace quill
