#pragma once

#include "quill/Delta.h"
#include "quill/Formats.h"

#include <QStringList>

class QTextBlock;
class QTextDocument;

namespace quill {

struct LoadOptions
{
    StyleOptions style;
    /// Base URL for relative image URLs ("assets/x.png"). Empty: relative
    /// URLs resolve against the current directory.
    QUrl baseUrl;
};

struct LoadResult
{
    /// False when nothing was loaded (the Delta is not a document).
    bool ok = false;
    /// Non-fatal findings: unknown attributes, unsupported values, a missing
    /// trailing newline. Everything reported here is still preserved and
    /// round-trips unchanged; it just has no visual effect.
    QStringList warnings;
    QString error;
};

/// Conversion between Delta documents and QTextDocument.
class DeltaDocument
{
public:
    /// Replaces the contents of `doc` with the document Delta `delta`.
    /// Undo history is cleared and the document is marked unmodified.
    static LoadResult load(QTextDocument* doc, const Delta& delta, const LoadOptions& options = {});

    /// Recomputes list membership for the blocks from `first` to `last`
    /// (inclusive, extended to the whole contiguous run of list lines around
    /// them) from their line attributes, reproducing flutter_quill's numbering:
    /// a deeper level does not interrupt its parent list, a different list
    /// type at the same level starts a new list, any non-list line ends all.
    static void regroupLists(const QTextBlock& first, const QTextBlock& last,
                             const StyleOptions& style);

    /// The style options `load` actually used for `doc` (base font filled in).
    static StyleOptions effectiveStyle(const QTextDocument* doc, const StyleOptions& style);
};

} // namespace quill
