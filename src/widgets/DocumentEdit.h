#pragma once

// Helpers shared by the loader (DeltaDocument.cpp), the change applier
// (DocumentEdit.cpp) and QuillEditor. Not installed.

#include "quill/Delta.h"
#include "quill/Formats.h"

#include <QTextCharFormat>

class QTextBlock;
class QTextDocument;

namespace quill::detail {

/// The keys of `attrs` that are inline formats (formats::inlineKeys()).
AttributeMap inlineSubset(const AttributeMap& attrs);

/// Char format of a block with line attributes `line`; what text typed into
/// an empty line gets.
QTextCharFormat blockCharFormat(const AttributeMap& line, const StyleOptions& style);

/// Char format of the object replacement character representing `embed`
/// (e.g. {"image": "x.png"}) with op attributes `attrs` on a line `line`.
QTextCharFormat embedCharFormat(const QJsonObject& embed, const AttributeMap& attrs, const AttributeMap& line,
                                const StyleOptions& style);

/// Decodes a data: image URL into a document resource; other URLs are left
/// to QTextDocument (file, qrc) or the editor (http, M7).
void registerImageResource(QTextDocument* doc, const QString& source);

/// Text as QTextDocument can store it: "\r\n", "\r", U+2028 and U+2029
/// become "\n"; Qt's frame markers U+FDD0/U+FDD1 become U+FFFD.
QString normalizeText(const QString& text);

/// Restyles `block` to `line`, the Delta of the line including its "\n",
/// whose text must equal the block's text. List membership is kept; call
/// DeltaDocument::regroupLists afterwards. Formats that are already right are
/// not touched, so unchanged text does not show up in contentsChange.
void restyleBlock(const QTextBlock& block, const Delta& line, const StyleOptions& style);

/// The change that replaces [index, index + length) of `contents` with
/// `embed`. With `block`, flutter_quill's InsertEmbedsRule: the embed gets a
/// line of its own ("\n" before it with the line's attributes when it is
/// not at a line start, a plain "\n" after it when it is not at a line end).
/// `embedIndex` receives the embed's index in the result.
Delta embedInsertChange(const Delta& contents, int index, int length, const QJsonObject& embed, bool block,
                        int* embedIndex = nullptr);

/// flutter_quill's delete rules for deleting [index, index + length) of the
/// document `contents`: EnsureLastLineBreakDeleteRule (the final "\n"
/// stays), PreserveLineStyleOnMergeRule (deleting a range that starts with
/// a line's "\n" gives the merged line that line's attributes, unless the
/// line is empty and the next is not), else a plain delete.
Delta flutterDeleteChange(const Delta& contents, int index, int length);

/// Attributes of the last op of `delta` (for a line Delta: the line attributes).
AttributeMap lastAttributes(const Delta& delta);

/// DeltaDocument::apply(); `exact` (optional) is set when the result is
/// exactly contents.compose(change), i.e. the change needed no
/// normalisation.
Delta applyChange(QTextDocument* doc, const Delta& contents, const Delta& change, bool* exact);

/// Number of the first line whose list membership differs from what
/// DeltaDocument::regroupLists would build from scratch, or -1 (verify mode).
int staleListGrouping(const QTextDocument* doc, const StyleOptions& style);
/// Whether updateBlockSpacing(first, last) would change anything.
bool blockSpacingStale(const QTextBlock& first, const QTextBlock& last, const StyleOptions& style);
/// Number of the first line whose vertical margins are not what
/// DeltaDocument::updateBlockSpacing would set, or -1 (verify mode).
int staleBlockSpacing(const QTextDocument* doc, const StyleOptions& style);

} // namespace quill::detail
