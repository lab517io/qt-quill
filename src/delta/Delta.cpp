// C++ port of slab/delta 5.1 (BSD 3-Clause, Copyright (c) 2022, Slab, Inc.).
// See THIRD_PARTY_NOTICES.md.

#include "quill/Delta.h"

#include "quill/OpIterator.h"

#include <QHash>
#include <QReadWriteLock>

#include <algorithm>

namespace quill {
namespace {

// Placeholder character for embeds in diff().
constexpr QChar kNullCharacter(u'\0');

struct HandlerRegistry
{
    QReadWriteLock lock;
    QHash<QString, EmbedHandler> handlers;
};

HandlerRegistry& registry()
{
    static HandlerRegistry instance;
    return instance;
}

EmbedHandler getHandler(const QString& embedType)
{
    HandlerRegistry& r = registry();
    QReadLocker locker(&r.lock);
    const auto it = r.handlers.constFind(embedType);
    if (it == r.handlers.constEnd())
        throw DeltaError("no handlers for embed type \"" + embedType.toStdString() + "\"");
    return it.value();
}

// The JavaScript `typeof` of an insert/retain value, for error messages.
const char* jsTypeOf(const Op& op, bool insertSide)
{
    if (insertSide) {
        if (!op.isInsert())
            return "undefined";
        return op.isEmbed() ? "object" : "string";
    }
    if (!op.isRetain())
        return "undefined";
    return op.isEmbed() ? "object" : "number";
}

struct EmbedTypeAndData
{
    QString type;
    QJsonValue a;
    QJsonValue b;
};

// getEmbedTypeAndData(a, b): `a` is the insert or retain value of `aOp`
// (selected by aInsertSide), `b` is the embed of `bOp` on the side given.
EmbedTypeAndData getEmbedTypeAndData(const Op& aOp, bool aInsertSide, const Op& bOp, bool bInsertSide)
{
    const bool aIsObject = aOp.isEmbed() && (aInsertSide ? aOp.isInsert() : aOp.isRetain());
    if (!aIsObject)
        throw DeltaError(std::string("cannot retain a ") + jsTypeOf(aOp, aInsertSide));
    const bool bIsObject = bOp.isEmbed() && (bInsertSide ? bOp.isInsert() : bOp.isRetain());
    if (!bIsObject)
        throw DeltaError(std::string("cannot retain a ") + jsTypeOf(bOp, bInsertSide));
    const QString embedType = aOp.embed().isEmpty() ? QString() : aOp.embed().begin().key();
    const QString otherType = bOp.embed().isEmpty() ? QString() : bOp.embed().begin().key();
    if (embedType.isEmpty() || embedType != otherType) {
        throw DeltaError("embed types not matched: " + embedType.toStdString() + " != " + otherType.toStdString());
    }
    return {embedType, aOp.embed().value(embedType), bOp.embed().value(embedType)};
}

} // namespace

// --- Builder --------------------------------------------------------------------

Delta& Delta::insert(const QString& text, const AttributeMap& attributes)
{
    if (text.isEmpty())
        return *this;
    return push(Op::insert(text, attributes));
}

Delta& Delta::insert(const QJsonObject& embed, const AttributeMap& attributes)
{
    return push(Op::insert(embed, attributes));
}

Delta& Delta::retain(qsizetype length, const AttributeMap& attributes)
{
    if (length <= 0)
        return *this;
    return push(Op::retain(length, attributes));
}

Delta& Delta::retain(const QJsonObject& embed, const AttributeMap& attributes)
{
    return push(Op::retain(embed, attributes));
}

Delta& Delta::remove(qsizetype length)
{
    if (length <= 0)
        return *this;
    return push(Op::remove(length));
}

Delta& Delta::push(const Op& newOp)
{
    qsizetype index = m_ops.size();
    if (index > 0) {
        const Op* lastOp = &m_ops.at(index - 1);
        if (newOp.isDelete() && lastOp->isDelete()) {
            m_ops[index - 1] = Op::remove(lastOp->count() + newOp.count());
            return *this;
        }
        // Since it does not matter if we insert before or after deleting at the
        // same index, always prefer to insert first.
        if (lastOp->isDelete() && newOp.isInsert()) {
            index -= 1;
            if (index == 0) {
                m_ops.prepend(newOp);
                return *this;
            }
            lastOp = &m_ops.at(index - 1);
        }
        if (newOp.attributes() == lastOp->attributes()) {
            if (newOp.isTextInsert() && lastOp->isTextInsert()) {
                // In place: building a new string each time is quadratic
                // for long runs of pushes (exporting plain text line by line).
                m_ops[index - 1].appendText(newOp.text());
                return *this;
            }
            if (newOp.isCountRetain() && lastOp->isCountRetain()) {
                m_ops[index - 1] = Op::retain(lastOp->count() + newOp.count(), newOp.attributes());
                return *this;
            }
        }
    }
    m_ops.insert(index, newOp);
    return *this;
}

Delta& Delta::chop()
{
    if (!m_ops.isEmpty()) {
        const Op& lastOp = m_ops.back();
        if (lastOp.isCountRetain() && !lastOp.hasAttributes())
            m_ops.removeLast();
    }
    return *this;
}

// --- Inspection -------------------------------------------------------------------

bool Delta::isDocument() const
{
    return std::all_of(m_ops.cbegin(), m_ops.cend(), [](const Op& op) { return op.isInsert(); });
}

qsizetype Delta::length() const
{
    qsizetype length = 0;
    for (const Op& op : m_ops)
        length += op.length();
    return length;
}

qsizetype Delta::changeLength() const
{
    qsizetype length = 0;
    for (const Op& op : m_ops) {
        if (op.isInsert())
            length += op.length();
        else if (op.isDelete())
            length -= op.count();
    }
    return length;
}

QList<Op> Delta::filter(const std::function<bool(const Op&, qsizetype)>& predicate) const
{
    QList<Op> result;
    for (qsizetype i = 0; i < m_ops.size(); ++i) {
        if (predicate(m_ops.at(i), i))
            result.append(m_ops.at(i));
    }
    return result;
}

void Delta::forEach(const std::function<void(const Op&, qsizetype)>& fn) const
{
    for (qsizetype i = 0; i < m_ops.size(); ++i)
        fn(m_ops.at(i), i);
}

std::pair<QList<Op>, QList<Op>> Delta::partition(const std::function<bool(const Op&)>& predicate) const
{
    std::pair<QList<Op>, QList<Op>> result;
    for (const Op& op : m_ops)
        (predicate(op) ? result.first : result.second).append(op);
    return result;
}

// --- Document operations ------------------------------------------------------------

Delta Delta::slice(qsizetype start, qsizetype end) const
{
    QList<Op> ops;
    OpIterator iter(m_ops);
    qsizetype index = start > 0 ? iter.skip(start) : 0;
    while (index < end && iter.hasNext()) {
        const Op nextOp = iter.next(end - index);
        ops.append(nextOp);
        index += nextOp.length();
    }
    return Delta(std::move(ops));
}

Delta Delta::concat(const Delta& other) const
{
    Delta delta(m_ops);
    if (!other.m_ops.isEmpty()) {
        delta.push(other.m_ops.front());
        delta.m_ops.append(other.m_ops.mid(1));
    }
    return delta;
}

Delta Delta::diff(const Delta& other, qsizetype cursor) const
{
    return diff(other, fastdiff::CursorInfo{{cursor, 0}, std::nullopt});
}

Delta Delta::diff(const Delta& other, const std::optional<fastdiff::CursorInfo>& cursor) const
{
    const auto toString = [](const Delta& delta, const char* prep) {
        QString s;
        for (const Op& op : delta.m_ops) {
            if (!op.isInsert())
                throw DeltaError(std::string("diff() called ") + prep + " non-document");
            if (op.isEmbed())
                s += kNullCharacter;
            else
                s += op.text();
        }
        return s;
    };
    const QString thisString = toString(*this, "with");
    const QString otherString = toString(other, "on");

    Delta retDelta;
    const QList<fastdiff::Diff> diffResult = fastdiff::diff(thisString, otherString, cursor, true);
    OpIterator thisIter(m_ops);
    OpIterator otherIter(other.m_ops);
    for (const fastdiff::Diff& component : diffResult) {
        qsizetype length = component.second.size();
        while (length > 0) {
            qsizetype opLength = 0;
            switch (component.first) {
            case fastdiff::Operation::Insert:
                opLength = std::min(otherIter.peekLength(), length);
                retDelta.push(otherIter.next(opLength));
                break;
            case fastdiff::Operation::Delete:
                opLength = std::min(length, thisIter.peekLength());
                thisIter.next(opLength);
                retDelta.remove(opLength);
                break;
            case fastdiff::Operation::Equal: {
                opLength = std::min({thisIter.peekLength(), otherIter.peekLength(), length});
                const Op thisOp = thisIter.next(opLength);
                const Op otherOp = otherIter.next(opLength);
                const bool sameInsert = thisOp.isEmbed() == otherOp.isEmbed() && thisOp.text() == otherOp.text()
                                        && thisOp.embed() == otherOp.embed();
                if (sameInsert) {
                    retDelta.retain(opLength, attributes::diff(thisOp.attributes(), otherOp.attributes()));
                } else {
                    retDelta.push(otherOp).remove(opLength);
                }
                break;
            }
            }
            length -= opLength;
        }
    }
    return retDelta.chop();
}

void Delta::eachLine(const std::function<bool(const Delta&, const AttributeMap&, qsizetype)>& fn,
                     const QString& newline) const
{
    OpIterator iter(m_ops);
    Delta line;
    qsizetype i = 0;
    while (iter.hasNext()) {
        if (iter.peekType() != Op::Type::Insert)
            return;
        const Op* thisOp = iter.peek();
        const qsizetype start = thisOp->length() - iter.peekLength();
        const qsizetype index = thisOp->isEmbed() ? -1 : thisOp->text().indexOf(newline, start) - start;
        if (index < 0) {
            line.push(iter.next());
        } else if (index > 0) {
            line.push(iter.next(index));
        } else {
            if (!fn(line, iter.next(1).attributes(), i))
                return;
            i += 1;
            line = Delta();
        }
    }
    if (line.length() > 0)
        fn(line, {}, i);
}

Delta Delta::invert(const Delta& base) const
{
    Delta inverted;
    qsizetype baseIndex = 0;
    for (const Op& op : m_ops) {
        if (op.isInsert()) {
            inverted.remove(op.length());
        } else if (op.isCountRetain() && !op.hasAttributes()) {
            inverted.retain(op.count());
            baseIndex += op.count();
        } else if (op.isDelete() || op.isCountRetain()) {
            const qsizetype length = op.count();
            const Delta slice = base.slice(baseIndex, baseIndex + length);
            for (const Op& baseOp : slice.m_ops) {
                if (op.isDelete()) {
                    inverted.push(baseOp);
                } else if (op.isRetain() && op.hasAttributes()) {
                    inverted.retain(baseOp.length(), attributes::invert(op.attributes(), baseOp.attributes()));
                }
            }
            baseIndex += length;
        } else if (op.isRetain() && op.isEmbed()) {
            const Delta slice = base.slice(baseIndex, baseIndex + 1);
            OpIterator sliceIter(slice.m_ops);
            const Op baseOp = sliceIter.next();
            const EmbedTypeAndData e = getEmbedTypeAndData(op, false, baseOp, true);
            const EmbedHandler handler = getHandler(e.type);
            QJsonObject embed;
            embed.insert(e.type, handler.invert(e.a, e.b));
            inverted.retain(embed, attributes::invert(op.attributes(), baseOp.attributes()));
            baseIndex += 1;
        }
    }
    return inverted.chop();
}

// --- Operational transform ------------------------------------------------------------

Delta Delta::compose(const Delta& other) const
{
    OpIterator thisIter(m_ops);
    OpIterator otherIter(other.m_ops);
    QList<Op> ops;
    const Op* firstOther = otherIter.peek();
    if (firstOther && firstOther->isCountRetain() && !firstOther->hasAttributes()) {
        // The inserts before the change are copied unchanged, in one go.
        qsizetype firstLeft = firstOther->count();
        qsizetype prefix = 0;
        while (prefix < m_ops.size() && m_ops.at(prefix).isInsert() && m_ops.at(prefix).length() <= firstLeft) {
            firstLeft -= m_ops.at(prefix).length();
            ++prefix;
        }
        ops.reserve(m_ops.size() + other.m_ops.size());
        ops.append(m_ops.constBegin(), m_ops.constBegin() + prefix);
        thisIter.skipOps(prefix);
        if (firstOther->count() - firstLeft > 0)
            otherIter.next(firstOther->count() - firstLeft);
    }
    Delta delta(std::move(ops));
    while (thisIter.hasNext() || otherIter.hasNext()) {
        if (otherIter.peekType() == Op::Type::Insert) {
            delta.push(otherIter.next());
        } else if (thisIter.peekType() == Op::Type::Delete) {
            delta.push(thisIter.next());
        } else {
            const qsizetype length = std::min(thisIter.peekLength(), otherIter.peekLength());
            const Op thisOp = thisIter.next(length);
            const Op otherOp = otherIter.next(length);
            if (otherOp.isRetain()) {
                Op newOp;
                if (thisOp.isCountRetain()) {
                    newOp = otherOp.isCountRetain() ? Op::retain(length) : Op::retain(otherOp.embed());
                } else if (otherOp.isCountRetain()) {
                    // thisOp is an insert or an embed retain
                    if (thisOp.isInsert())
                        newOp = thisOp.isEmbed() ? Op::insert(thisOp.embed()) : Op::insert(thisOp.text());
                    else
                        newOp = Op::retain(thisOp.embed());
                } else {
                    const bool thisIsInsert = thisOp.isInsert();
                    const EmbedTypeAndData e = getEmbedTypeAndData(thisOp, thisIsInsert, otherOp, false);
                    const EmbedHandler handler = getHandler(e.type);
                    QJsonObject embed;
                    embed.insert(e.type, handler.compose(e.a, e.b, !thisIsInsert));
                    newOp = thisIsInsert ? Op::insert(embed) : Op::retain(embed);
                }
                // Preserve null when composing with a retain, otherwise remove it for inserts
                newOp.setAttributes(
                    attributes::compose(thisOp.attributes(), otherOp.attributes(), thisOp.isCountRetain()));
                delta.push(newOp);

                // Optimization if rest of other is just retain
                if (!otherIter.hasNext() && delta.m_ops.back() == newOp) {
                    // Copy the rest unchanged, in place (concat() would copy
                    // everything again).
                    if (thisIter.hasNext()) {
                        delta.push(thisIter.next());
                        delta.m_ops.append(m_ops.constBegin() + thisIter.index(), m_ops.constEnd());
                    }
                    return delta.chop();
                }
                // Other op should be delete, we could be an insert or retain
                // Insert + delete cancels out
            } else if (otherOp.isDelete() && thisOp.isRetain()) {
                delta.push(otherOp);
            }
        }
    }
    return delta.chop();
}

Delta Delta::transform(const Delta& other, bool priority) const
{
    OpIterator thisIter(m_ops);
    OpIterator otherIter(other.m_ops);
    Delta delta;
    while (thisIter.hasNext() || otherIter.hasNext()) {
        if (thisIter.peekType() == Op::Type::Insert && (priority || otherIter.peekType() != Op::Type::Insert)) {
            delta.retain(thisIter.next().length());
        } else if (otherIter.peekType() == Op::Type::Insert) {
            delta.push(otherIter.next());
        } else {
            const qsizetype length = std::min(thisIter.peekLength(), otherIter.peekLength());
            const Op thisOp = thisIter.next(length);
            const Op otherOp = otherIter.next(length);
            if (thisOp.isDelete()) {
                // Our delete either makes their delete redundant or removes their retain
                continue;
            } else if (otherOp.isDelete()) {
                delta.push(otherOp);
            } else {
                const AttributeMap attrs = attributes::transform(thisOp.attributes(), otherOp.attributes(), priority);
                if (!otherOp.isEmbed()) {
                    // We retain either their retain or insert
                    delta.retain(length, attrs);
                    continue;
                }
                QJsonObject transformedData = otherOp.embed();
                if (thisOp.isEmbed()) {
                    const QString embedType = thisOp.embed().isEmpty() ? QString() : thisOp.embed().begin().key();
                    if (!embedType.isEmpty() && otherOp.embed().contains(embedType)
                        && otherOp.embed().begin().key() == embedType) {
                        const EmbedHandler handler = getHandler(embedType);
                        QJsonObject embed;
                        embed.insert(embedType, handler.transform(thisOp.embed().value(embedType),
                                                                  otherOp.embed().value(embedType), priority));
                        transformedData = embed;
                    }
                }
                delta.retain(transformedData, attrs);
            }
        }
    }
    return delta.chop();
}

qsizetype Delta::transformPosition(qsizetype index, bool priority) const
{
    OpIterator thisIter(m_ops);
    qsizetype offset = 0;
    while (thisIter.hasNext() && offset <= index) {
        const qsizetype length = thisIter.peekLength();
        const Op::Type nextType = thisIter.peekType();
        thisIter.next();
        if (nextType == Op::Type::Delete) {
            index -= std::min(length, index - offset);
            continue;
        } else if (nextType == Op::Type::Insert && (offset < index || !priority)) {
            index += length;
        }
        offset += length;
    }
    return index;
}

// --- Embed handlers ---------------------------------------------------------------------

void Delta::registerEmbed(const QString& embedType, EmbedHandler handler)
{
    HandlerRegistry& r = registry();
    QWriteLocker locker(&r.lock);
    r.handlers.insert(embedType, std::move(handler));
}

void Delta::unregisterEmbed(const QString& embedType)
{
    HandlerRegistry& r = registry();
    QWriteLocker locker(&r.lock);
    r.handlers.remove(embedType);
}

} // namespace quill
