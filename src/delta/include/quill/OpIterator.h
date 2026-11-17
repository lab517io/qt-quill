#pragma once

#include "quill/Op.h"

#include <QList>

namespace quill {

/// Walks a list of operations, splitting them at arbitrary lengths.
/// Mirrors slab/delta's OpIterator, including returning retain(Infinity)
/// once exhausted.
class OpIterator
{
public:
    explicit OpIterator(const QList<Op>& ops) : m_ops(ops) {}

    bool hasNext() const { return peekLength() < Op::Infinity; }
    /// Returns the next `length` units of the current op (all of it when
    /// `length` is 0 or larger than what is left).
    Op next(qsizetype length = Op::Infinity);
    /// The current op, unsplit, or nullptr when exhausted.
    const Op* peek() const;
    qsizetype peekLength() const;
    /// Type of the current op; Retain when exhausted.
    Op::Type peekType() const;
    /// The remaining ops, starting with what is left of the current one.
    QList<Op> rest();
    /// Index of the current op, and how much of it was consumed.
    qsizetype index() const { return m_index; }
    qsizetype offset() const { return m_offset; }
    /// Skips `count` whole ops; only at an op boundary (offset() == 0).
    void skipOps(qsizetype count) { m_index += count; }
    /// Skips `length` units (fewer when the ops end first) without building
    /// the skipped ops; returns how many were skipped.
    qsizetype skip(qsizetype length);

private:
    const QList<Op>& m_ops;
    qsizetype m_index = 0;
    qsizetype m_offset = 0;
};

} // namespace quill
