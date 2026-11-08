#pragma once

#include "quill/Attributes.h"
#include "quill/FastDiff.h"
#include "quill/Op.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace quill {

/// Thrown on misuse that slab/delta also rejects with an exception, e.g.
/// diff() on a non-document or retaining an embed without a handler. The
/// messages are the same as the JavaScript ones.
class DeltaError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Handler for changes inside embeds (`retain: {type: change}`), as
/// registered with Delta.registerEmbed() in Quill 2. flutter_quill does not
/// use embed changes; without a registered handler they raise DeltaError.
struct EmbedHandler
{
    std::function<QJsonValue(const QJsonValue& a, const QJsonValue& b, bool keepNull)> compose;
    std::function<QJsonValue(const QJsonValue& a, const QJsonValue& b)> invert;
    std::function<QJsonValue(const QJsonValue& a, const QJsonValue& b, bool priority)> transform;
};

/// A Quill Delta: a document (inserts only, ending with "\n") or a change.
///
/// Port of slab/delta 5.1 with identical semantics, including the exact
/// shape of results (operation splitting and merging), so Deltas produced
/// here compare equal to those of Quill.js and flutter_quill.
class Delta
{
public:
    /// JSON shape for serialisation.
    enum class JsonShape {
        Array,     ///< bare array of ops, as flutter_quill's Delta.toJson()
        OpsObject, ///< {"ops": [...]}, as Quill.js
    };

    Delta() = default;
    /// Takes the ops as they are, without merging (like `new Delta(ops)`).
    explicit Delta(QList<Op> ops) : m_ops(std::move(ops)) {}

    // --- Builder ------------------------------------------------------------
    Delta& insert(const QString& text, const AttributeMap& attributes = {});
    Delta& insert(const QJsonObject& embed, const AttributeMap& attributes = {});
    Delta& retain(qsizetype length, const AttributeMap& attributes = {});
    Delta& retain(const QJsonObject& embed, const AttributeMap& attributes = {});
    /// The Delta "delete" operation.
    Delta& remove(qsizetype length);
    /// Appends an op, merging with the previous one where possible and
    /// keeping inserts before an adjacent delete.
    Delta& push(const Op& op);
    /// Removes a trailing retain without attributes.
    Delta& chop();

    // --- Inspection -----------------------------------------------------------
    const QList<Op>& ops() const { return m_ops; }
    bool isEmpty() const { return m_ops.isEmpty(); }
    /// True when all ops are inserts.
    bool isDocument() const;
    qsizetype length() const;
    qsizetype changeLength() const;

    QList<Op> filter(const std::function<bool(const Op&, qsizetype)>& predicate) const;
    void forEach(const std::function<void(const Op&, qsizetype)>& fn) const;
    std::pair<QList<Op>, QList<Op>> partition(const std::function<bool(const Op&)>& predicate) const;
    template <class T, class F>
    T reduce(F fn, T initial) const
    {
        for (qsizetype i = 0; i < m_ops.size(); ++i)
            initial = fn(std::move(initial), m_ops.at(i), i);
        return initial;
    }
    template <class F>
    auto map(F fn) const -> QList<decltype(fn(std::declval<const Op&>(), qsizetype()))>
    {
        QList<decltype(fn(std::declval<const Op&>(), qsizetype()))> result;
        result.reserve(m_ops.size());
        for (qsizetype i = 0; i < m_ops.size(); ++i)
            result.append(fn(m_ops.at(i), i));
        return result;
    }

    // --- Document operations ----------------------------------------------------
    Delta slice(qsizetype start = 0, qsizetype end = Op::Infinity) const;
    Delta concat(const Delta& other) const;
    /// Returns the change turning this document into `other`. Both must be
    /// documents, otherwise DeltaError is thrown. The cursor hint resolves
    /// ambiguous diffs the same way Quill does.
    Delta diff(const Delta& other, const std::optional<fastdiff::CursorInfo>& cursor = {}) const;
    Delta diff(const Delta& other, qsizetype cursor) const;
    /// Calls `fn(line, lineAttributes, index)` for each line; return false to stop.
    void eachLine(const std::function<bool(const Delta&, const AttributeMap&, qsizetype)>& fn,
                  const QString& newline = QStringLiteral("\n")) const;
    /// Returns the change that undoes this change applied to `base`.
    Delta invert(const Delta& base) const;

    // --- Operational transform ----------------------------------------------------
    Delta compose(const Delta& other) const;
    /// Transforms `other` against this concurrent change. `priority` means
    /// this change happened first.
    Delta transform(const Delta& other, bool priority = false) const;
    qsizetype transformPosition(qsizetype index, bool priority = false) const;

    // --- Embed handlers -------------------------------------------------------------
    static void registerEmbed(const QString& embedType, EmbedHandler handler);
    static void unregisterEmbed(const QString& embedType);

    // --- JSON ---------------------------------------------------------------------
    /// Accepts a bare array of ops or an object with "ops". Ops are taken as
    /// they are, like `new Delta(ops)`. Returns nullopt and sets `error` on
    /// invalid input; nothing is silently dropped except empty inserts and
    /// zero-length retains/deletes, which carry no meaning.
    static std::optional<Delta> fromJson(const QJsonValue& json, QString* error = nullptr);
    static std::optional<Delta> fromJson(const QByteArray& json, QString* error = nullptr);
    QJsonArray toJsonArray() const;
    QJsonObject toJsonObject() const;
    /// Compact UTF-8 JSON. Each op is written with its operation key first
    /// and "attributes" last, as Quill.js and flutter_quill write them.
    QByteArray toJson(JsonShape shape = JsonShape::Array) const;

    bool operator==(const Delta& other) const { return m_ops == other.m_ops; }
    bool operator!=(const Delta& other) const { return !(*this == other); }

private:
    QList<Op> m_ops;
};

} // namespace quill
