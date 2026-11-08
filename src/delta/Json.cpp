#include "quill/Delta.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace quill {
namespace {

// Compact JSON of a single value (QJsonDocument only serialises containers).
QByteArray valueToJson(const QJsonValue& value)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return wrapped.mid(1, wrapped.size() - 2);
}

void appendOp(QByteArray& out, const Op& op)
{
    out += '{';
    switch (op.type()) {
    case Op::Type::Insert:
        out += "\"insert\":";
        out += op.isEmbed() ? valueToJson(op.embed()) : valueToJson(op.text());
        break;
    case Op::Type::Retain:
        out += "\"retain\":";
        out += op.isEmbed() ? valueToJson(op.embed()) : QByteArray::number(qint64(op.count()));
        break;
    case Op::Type::Delete:
        out += "\"delete\":";
        out += QByteArray::number(qint64(op.count()));
        break;
    }
    if (op.hasAttributes() && !op.isDelete()) {
        out += ",\"attributes\":";
        out += valueToJson(op.attributes());
    }
    out += '}';
}

} // namespace

std::optional<Delta> Delta::fromJson(const QJsonValue& json, QString* error)
{
    QJsonArray array;
    if (json.isArray()) {
        array = json.toArray();
    } else if (json.isObject() && json.toObject().value(QStringLiteral("ops")).isArray()) {
        array = json.toObject().value(QStringLiteral("ops")).toArray();
    } else {
        if (error)
            *error = QStringLiteral("expected an array of ops or an object with \"ops\"");
        return std::nullopt;
    }
    QList<Op> ops;
    ops.reserve(array.size());
    for (qsizetype i = 0; i < array.size(); ++i) {
        QString opError;
        std::optional<Op> op = Op::fromJson(array.at(i), &opError);
        if (!op) {
            if (error)
                *error = QStringLiteral("op %1: %2").arg(i).arg(opError);
            return std::nullopt;
        }
        if (op->length() == 0)
            continue; // empty insert, retain 0 or delete 0: no meaning
        ops.append(*op);
    }
    return Delta(std::move(ops));
}

std::optional<Delta> Delta::fromJson(const QByteArray& json, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("JSON parse error at offset %1: %2")
                         .arg(parseError.offset)
                         .arg(parseError.errorString());
        return std::nullopt;
    }
    if (doc.isArray())
        return fromJson(QJsonValue(doc.array()), error);
    return fromJson(QJsonValue(doc.object()), error);
}

QJsonArray Delta::toJsonArray() const
{
    QJsonArray array;
    for (const Op& op : m_ops)
        array.append(op.toJson());
    return array;
}

QJsonObject Delta::toJsonObject() const
{
    QJsonObject object;
    object.insert(QStringLiteral("ops"), toJsonArray());
    return object;
}

QByteArray Delta::toJson(JsonShape shape) const
{
    QByteArray out;
    if (shape == JsonShape::OpsObject)
        out += "{\"ops\":";
    out += '[';
    for (qsizetype i = 0; i < m_ops.size(); ++i) {
        if (i > 0)
            out += ',';
        appendOp(out, m_ops.at(i));
    }
    out += ']';
    if (shape == JsonShape::OpsObject)
        out += '}';
    return out;
}

} // namespace quill
