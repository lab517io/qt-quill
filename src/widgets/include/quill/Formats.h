#pragma once

// Mapping between Quill Delta attributes and Qt rich text formats.
//
// Every format created here is a pure function of the attributes (and the
// StyleOptions), and it also stores the original attribute JSON. The
// exporter (M4) recomputes the expected format from the stored JSON and
// derives attribute changes only from properties that differ, so styling
// added by the loader (header sizes, code backgrounds, link colours) never
// leaks into the exported Delta.

#include "quill/Attributes.h"
#include "quill/Colors.h"

#include <QColor>
#include <QMetaType>
#include <QFont>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFormat>
#include <QTextListFormat>

namespace quill {

/// Custom QTextFormat properties used by quill-qt. Consumers may use
/// QTextFormat::UserProperty + 0x100 and above.
namespace prop {
/// QTextCharFormat: the op's original inline attributes (QJsonObject).
constexpr int InlineAttributes = QTextFormat::UserProperty + 0x10;
/// QTextBlockFormat: the line's original attributes, i.e. those of its "\n".
constexpr int BlockAttributes = QTextFormat::UserProperty + 0x11;
/// QTextBlockFormat: BlockKind as int.
constexpr int BlockKind = QTextFormat::UserProperty + 0x12;
/// QTextCharFormat of an embed: its key, e.g. "image", "video", "custom".
constexpr int EmbedType = QTextFormat::UserProperty + 0x13;
/// QTextCharFormat of an embed: the whole embed object as in the Delta,
/// e.g. {"image": "https://..."} (QJsonObject).
constexpr int Embed = QTextFormat::UserProperty + 0x14;
/// Object type of non-image embeds; M7 registers a QTextObjectInterface for it.
constexpr int EmbedObjectType = QTextFormat::UserObject + 1;
} // namespace prop

enum class BlockKind { Paragraph = 0, Header, ListItem, Blockquote, CodeBlock };

/// Visual parameters for the mapping. Defaults follow flutter_quill's light
/// DefaultStyles; sizes are relative to its 16 px base so a document keeps
/// its proportions at any Qt base font.
struct StyleOptions
{
    /// Base font. When unset (family empty), the document's default font is used.
    QFont baseFont = QFont(QString());
    /// The base font size, in Delta size units, that baseFont corresponds to.
    /// flutter_quill's paragraph size is 16; numeric "size" values and the
    /// factors below are relative to it.
    double referenceSize = 16.0;
    ColorDialect colorDialect = ColorDialect::Flutter;

    /// flutter_quill header sizes 34/30/24/20/18/16 at a 16 base.
    double headerSizes[6] = {34, 30, 24, 20, 18, 16};
    /// flutter_quill named sizes small/large/huge: 10/18/22. ("normal" = base.)
    double sizeSmall = 10, sizeLarge = 18, sizeHuge = 22;
    /// flutter_quill's boolean "small" attribute: 12.
    double smallAttributeSize = 12;
    double inlineCodeSize = 14;
    double codeBlockSize = 13;

    /// Width of one indent level and of the list marker column, in pixels.
    /// flutter_quill uses one font size per indent level.
    double indentWidth = 24;
    /// Extra left margin of blockquote lines (flutter_quill: one font size).
    double blockquoteMargin = 16;
    /// Left and right margin of code-block lines, inside the code background.
    double codeBlockPadding = 8;
    /// Set the code-block background on each block format, which is what a
    /// plain QTextDocument can render. QuillEditor turns this off and paints
    /// one rounded panel behind each run of code lines instead.
    bool codeBlockBackgroundOnBlocks = true;

    QStringList monospaceFamilies{QStringLiteral("Roboto Mono"), QStringLiteral("Menlo"),
                                  QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Consolas"),
                                  QStringLiteral("monospace")};
    QColor linkColor = QColor(0x15, 0x65, 0xC0);
    QColor inlineCodeBackground = QColor(0xF5, 0xF5, 0xF5);   // Colors.grey.shade100
    QColor codeBlockBackground = QColor(0xFA, 0xFA, 0xFA);    // Colors.grey.shade50
    QColor codeBlockForeground = QColor(0x0D, 0x47, 0xA1, 230); // blue.shade900, 90%
    QColor blockquoteForeground = QColor(0, 0, 0, 153);       // text at 60%
    /// Bar left of blockquote lines, painted by QuillEditor (grey.shade300).
    QColor blockquoteBarColor = QColor(0xE0, 0xE0, 0xE0);
    double blockquoteBarWidth = 4;

    /// Point size for a size expressed in Delta units (relative to referenceSize).
    double pointSizeFor(double deltaSize) const;
    double basePointSize() const;
};

namespace formats {

/// Attribute keys quill-qt understands, by scope. Anything else is preserved
/// without visual effect.
const QSet<QString>& inlineKeys();
const QSet<QString>& lineKeys();
/// flutter_quill "ignore" scope keys (width/height/style/token) and other
/// keys preserved without visual effect.
const QSet<QString>& passiveKeys();

/// Char format for text with `inlineAttrs` on a line with `lineAttrs`.
QTextCharFormat charFormat(const AttributeMap& inlineAttrs, const AttributeMap& lineAttrs,
                           const StyleOptions& style);

/// Block format for a line. List membership is not set here; see
/// DeltaDocument::regroupLists().
QTextBlockFormat blockFormat(const AttributeMap& lineAttrs, const StyleOptions& style);

/// List format for a list line, or an invalid format when the line is not a
/// list item. Checked/unchecked lines share one list style; the check state
/// is the block marker.
QTextListFormat listFormat(const AttributeMap& lineAttrs, const StyleOptions& style);

/// Grouping key of a list line ("bullet", "ordered", "check"), or empty.
QString listGroup(const AttributeMap& lineAttrs);
/// Indent level of a line (attribute "indent", 0 when absent or invalid).
int indentLevel(const AttributeMap& lineAttrs);

/// Validates attribute values and reports problems ("header: 7 is not 1..6").
/// Unknown keys are not problems; they are reported separately by the loader.
QStringList validate(const AttributeMap& attrs, bool line, const StyleOptions& style);

} // namespace formats
} // namespace quill

Q_DECLARE_METATYPE(quill::StyleOptions)
