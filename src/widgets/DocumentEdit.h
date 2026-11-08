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
QTextCharFormat embedCharFormat(const QJsonObject& embed, const AttributeMap& attrs,
                                const AttributeMap& line, const StyleOptions& style);

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

/// Attributes of the last op of `delta` (for a line Delta: the line attributes).
AttributeMap lastAttributes(const Delta& delta);

} // namespace quill::detail
