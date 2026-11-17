// C++ port of slab/delta 5.1 (BSD 3-Clause, Copyright (c) 2022, Slab, Inc.).
// See THIRD_PARTY_NOTICES.md.

#include "quill/OpIterator.h"

namespace quill {

Op OpIterator::next(qsizetype length)
{
    if (length <= 0)
        length = Op::Infinity;
    if (m_index >= m_ops.size())
        return Op::retain(Op::Infinity);

    const Op& nextOp = m_ops.at(m_index);
    const qsizetype offset = m_offset;
    const qsizetype opLength = nextOp.length();
    if (length >= opLength - offset) {
        length = opLength - offset;
        m_index += 1;
        m_offset = 0;
    } else {
        m_offset += length;
    }
    switch (nextOp.type()) {
    case Op::Type::Delete:
        return Op::remove(length);
    case Op::Type::Retain:
        if (nextOp.isEmbed())
            return Op::retain(nextOp.embed(), nextOp.attributes());
        return Op::retain(length, nextOp.attributes());
    case Op::Type::Insert:
        if (nextOp.isEmbed())
            return Op::insert(nextOp.embed(), nextOp.attributes());
        return Op::insert(nextOp.text().mid(offset, length), nextOp.attributes());
    }
    Q_UNREACHABLE();
}

qsizetype OpIterator::skip(qsizetype length)
{
    qsizetype skipped = 0;
    while (length > 0 && m_index < m_ops.size()) {
        const qsizetype left = m_ops.at(m_index).length() - m_offset;
        if (length >= left) {
            skipped += left;
            length -= left;
            ++m_index;
            m_offset = 0;
        } else {
            m_offset += length;
            skipped += length;
            length = 0;
        }
    }
    return skipped;
}

const Op* OpIterator::peek() const
{
    return m_index < m_ops.size() ? &m_ops.at(m_index) : nullptr;
}

qsizetype OpIterator::peekLength() const
{
    if (m_index < m_ops.size())
        return m_ops.at(m_index).length() - m_offset;
    return Op::Infinity;
}

Op::Type OpIterator::peekType() const
{
    if (m_index < m_ops.size())
        return m_ops.at(m_index).type();
    return Op::Type::Retain;
}

QList<Op> OpIterator::rest()
{
    if (!hasNext())
        return {};
    if (m_offset == 0)
        return m_ops.mid(m_index);
    const qsizetype offset = m_offset;
    const qsizetype index = m_index;
    QList<Op> result{next()};
    result.append(m_ops.mid(m_index));
    m_offset = offset;
    m_index = index;
    return result;
}

} // namespace quill
