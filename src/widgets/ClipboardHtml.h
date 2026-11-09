#pragma once

// Normalisation of clipboard HTML from other applications before and after
// QTextDocument's HTML import (private header, milestone M9a). Follows what
// Quill's clipboard normalizers do for Word and Google Docs.

#include "quill/Delta.h"

#include <QString>
#include <QStringList>

class QTextDocument;

namespace quill::detail {

/// Cleans HTML before QTextDocument::setHtml: strips a raw CF_HTML header
/// and Apple's interchange newline, maps <strike> / <del> to <s>, and marks
/// Word's mso-list paragraphs (which are not <li>) with a list token.
QString normalizeClipboardHtml(const QString& html);

/// Gives blocks imported from <blockquote> and <pre> the Quill line formats
/// (QTextDocument keeps them only as BlockQuoteLevel / nonBreakableLines).
void markImportedBlocks(QTextDocument* doc);

/// Final cleanup of the exported Delta: monospace runs become "code", Word
/// list tokens become "list" / "indent", whitespace that HTML collapses at a
/// line end is dropped, and `ignored` attributes are removed.
Delta cleanPastedDelta(const Delta& delta, const QStringList& ignored);

} // namespace quill::detail
