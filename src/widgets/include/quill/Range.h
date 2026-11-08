#pragma once

#include <QMetaType>

namespace quill {

/// A selection or document range in Delta indices (== QTextDocument positions).
struct Range
{
    int index = 0;
    int length = 0;

    bool operator==(const Range& o) const { return index == o.index && length == o.length; }
    bool operator!=(const Range& o) const { return !(*this == o); }
};

} // namespace quill

Q_DECLARE_METATYPE(quill::Range)
