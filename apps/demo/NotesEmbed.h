#pragma once

#include <quill/EmbedRegistry.h>

/// Renderer for flutter_quill's example "notes" custom embed:
/// {"custom": "{\"notes\": \"<Delta JSON>\"}"}. Draws a card with the note's
/// text; activating it edits the text. Shows how a host adds custom types.
class NotesEmbed : public quill::CustomEmbedRenderer
{
public:
    NotesEmbed();
    QSizeF size(const quill::EmbedContext& c) const override;
    void paint(QPainter* painter, const QRectF& rect, const quill::EmbedContext& c) const override;
    bool activate(quill::QuillEditor* editor, const quill::EmbedContext& c) override;
    QString toolTip(const quill::EmbedContext& c) const override;

    /// Plain text of the note (its Delta's text inserts).
    static QString noteText(const quill::EmbedContext& c);
};
