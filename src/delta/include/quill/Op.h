#pragma once

#include "quill/Attributes.h"

#include <QJsonObject>
#include <QString>

#include <limits>
#include <optional>

namespace quill {

/// One Delta operation: insert (text or embed), retain (count or embed), or delete.
///
/// Lengths are UTF-16 code units, exactly like JavaScript string lengths, so
/// indices agree with Quill, flutter_quill and QTextDocument positions.
class Op
{
public:
    enum class Type { Insert, Retain, Delete };

    /// Length used for "until the end", the JavaScript `Infinity`.
    static constexpr qsizetype Infinity = std::numeric_limits<qsizetype>::max();

    /// A retain of `Infinity`, which is what an exhausted iterator returns.
    Op() = default;

    static Op insert(const QString& text, const AttributeMap& attributes = {});
    /// Inserts an embed such as {"image": "https://..."}; it has length 1.
    static Op insert(const QJsonObject& embed, const AttributeMap& attributes = {});
    static Op retain(qsizetype length, const AttributeMap& attributes = {});
    /// Retains an embed with a change to its data (Quill 2 embed handlers).
    static Op retain(const QJsonObject& embed, const AttributeMap& attributes = {});
    /// The Delta "delete" operation (`delete` is a C++ keyword).
    static Op remove(qsizetype length);

    Type type() const { return m_type; }
    bool isInsert() const { return m_type == Type::Insert; }
    bool isRetain() const { return m_type == Type::Retain; }
    bool isDelete() const { return m_type == Type::Delete; }
    /// True for an embed insert or an embed retain.
    bool isEmbed() const { return m_isEmbed; }
    bool isTextInsert() const { return isInsert() && !m_isEmbed; }
    /// True for a numeric retain (the JavaScript `typeof op.retain === 'number'`).
    bool isCountRetain() const { return isRetain() && !m_isEmbed; }

    /// Inserted text; empty for other operations.
    const QString& text() const { return m_text; }
    /// Embed object of an embed insert or embed retain; empty otherwise.
    const QJsonObject& embed() const { return m_embed; }
    /// Count of a numeric retain or a delete; 0 otherwise.
    qsizetype count() const { return m_count; }

    const AttributeMap& attributes() const { return m_attributes; }
    bool hasAttributes() const { return !m_attributes.isEmpty(); }
    void setAttributes(const AttributeMap& attributes) { m_attributes = attributes; }

    /// Length in the document: text length, 1 for embeds, count otherwise.
    qsizetype length() const;

    /// JSON form, e.g. {"insert": "a", "attributes": {"bold": true}}.
    QJsonObject toJson() const;
    /// Parses one operation. Returns nullopt and sets `error` when invalid.
    static std::optional<Op> fromJson(const QJsonValue& value, QString* error = nullptr);

    bool operator==(const Op& other) const;
    bool operator!=(const Op& other) const { return !(*this == other); }

private:
    Type m_type = Type::Retain;
    bool m_isEmbed = false;
    qsizetype m_count = Infinity;
    QString m_text;
    QJsonObject m_embed;
    AttributeMap m_attributes;
};

} // namespace quill
