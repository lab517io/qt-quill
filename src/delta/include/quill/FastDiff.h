#pragma once

// C++ port of fast-diff 1.3.0 (https://github.com/jhchen/fast-diff),
// itself derived from Google's diff-match-patch. Licensed under the Apache
// License 2.0; see THIRD_PARTY_NOTICES.md. Delta::diff() depends on producing
// exactly the same diffs as the JavaScript implementation, so this port
// follows the original line by line.

#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace quill::fastdiff {

enum class Operation { Delete = -1, Equal = 0, Insert = 1 };

using Diff = std::pair<Operation, QString>;

struct Range
{
    qsizetype index = 0;
    qsizetype length = 0;
};

/// Cursor hint. A plain cursor index is {oldRange = {index, 0}, no newRange}.
struct CursorInfo
{
    Range oldRange;
    std::optional<Range> newRange;
};

/// Diffs two strings (UTF-16 code units, never splitting surrogate pairs).
/// `cleanup` applies semantic cleanup, as Delta::diff() does.
QList<Diff> diff(const QString& text1, const QString& text2,
                 const std::optional<CursorInfo>& cursor = std::nullopt, bool cleanup = false);

} // namespace quill::fastdiff
