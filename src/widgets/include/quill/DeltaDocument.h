#pragma once

#include "quill/Delta.h"
#include "quill/Formats.h"

#include <QStringList>
#include <QUrl>

#include <optional>

class QTextBlock;
class QTextCharFormat;
class QTextDocument;

namespace quill {

struct LoadOptions
{
    StyleOptions style;
    /// Base URL for relative image URLs ("assets/x.png"). Empty: relative
    /// URLs resolve against the current directory.
    QUrl baseUrl;
};

struct ExportOptions
{
    /// Styling the document was loaded with. When unset, the options stored
    /// on the document by load() are used (see documentStyle()). Export
    /// compares formats against this styling, so it must match the load.
    std::optional<StyleOptions> style;
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

    /// Exports the whole document. The result is a document Delta ending
    /// with "\n" whose length equals doc->characterCount().
    static Delta toDelta(const QTextDocument* doc, const ExportOptions& options = {},
                         QStringList* warnings = nullptr);
    /// Exports positions [from, to). Equals toDelta(doc).slice(from, to) for
    /// documents without tables or frames.
    static Delta toDelta(const QTextDocument* doc, int from, int to,
                         const ExportOptions& options = {}, QStringList* warnings = nullptr);

    /// Applies the change Delta `change` to `doc`, whose current contents are
    /// `contents` (== toDelta(doc)), and returns the contents it now has.
    ///
    /// Delta semantics, with Quill's editing rules: inline attributes apply
    /// to text and embeds and line attributes to newlines only; `false`
    /// removes a known attribute; "\r" and Unicode line separators in inserted
    /// text become "\n"; operations past the end are dropped; the document
    /// always keeps a final "\n" (one without attributes when the change
    /// deletes it). Text is edited with QTextCursor calls in one edit block,
    /// so this is one undo step and one contentsChange signal.
    static Delta apply(QTextDocument* doc, const Delta& contents, const Delta& change);
    /// As above, exporting the current contents first.
    static Delta apply(QTextDocument* doc, const Delta& change);

    /// Inline attributes of text with format `format`: the stored attributes,
    /// updated for every property where `format` differs from what those
    /// attributes would produce on a line with `lineAttrs`. Pass the line
    /// attributes the text was styled for (the block's stored attributes).
    static AttributeMap inlineAttributes(const QTextCharFormat& format, const AttributeMap& lineAttrs,
                                         const StyleOptions& style);
    /// Line attributes of `block`, derived the same way, plus list membership.
    static AttributeMap lineAttributes(const QTextBlock& block, const StyleOptions& style);

    /// Recomputes list membership for the blocks from `first` to `last`
    /// (inclusive, extended to the whole contiguous run of list lines around
    /// them) from their line attributes, reproducing flutter_quill's numbering:
    /// a deeper level does not interrupt its parent list, a different list
    /// type at the same level starts a new list, any non-list line ends all.
    static void regroupLists(const QTextBlock& first, const QTextBlock& last,
                             const StyleOptions& style);

    /// The style options `load` actually used for `doc` (base font filled in).
    static StyleOptions effectiveStyle(const QTextDocument* doc, const StyleOptions& style);
    /// The style stored on `doc` by load(), or the defaults for `doc`.
    static StyleOptions documentStyle(const QTextDocument* doc);
    static void setDocumentStyle(QTextDocument* doc, const StyleOptions& style);
};

} // namespace quill
