// Loading Delta documents into QTextDocument (milestone M3).
#include "TestUtil.h"

#include <quill/DeltaDocument.h>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontInfo>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextList>

using namespace quill;

namespace {

Delta loadFixtureDelta(const QString& name)
{
    const QJsonDocument json = testutil::loadJson(name);
    QString error;
    const auto d = Delta::fromJson(json.isArray() ? QJsonValue(json.array()) : QJsonValue(json.object()), &error);
    if (!d)
        qFatal("fixture %s: %s", qPrintable(name), qPrintable(error));
    return *d;
}

QStringList allFixtures()
{
    QStringList names;
    const QDir dir(testutil::fixturePath(QStringLiteral("documents")));
    for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        names << QStringLiteral("documents/") + f;
    names << QStringLiteral("flutter_quill/example_default_sample.json");
    return names;
}

QImage render(QTextDocument& doc, int width = 600)
{
    doc.setTextWidth(width);
    QImage img(width, qMax(1, int(doc.size().height())), QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    doc.drawContents(&p);
    return img;
}

QTextCharFormat formatAt(const QTextDocument& doc, int position)
{
    // The format of the character at `position` (QTextCursor reports the one before).
    QTextCursor c(const_cast<QTextDocument*>(&doc));
    c.setPosition(position + 1);
    return c.charFormat();
}

QTextBlock blockWithText(const QTextDocument& doc, const QString& text)
{
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.text() == text)
            return b;
    }
    qFatal("no block with text %s", qPrintable(text));
}

int positionOf(const QTextDocument& doc, const QString& text)
{
    const QTextCursor c = doc.find(text);
    if (c.isNull())
        qFatal("text not found: %s", qPrintable(text));
    return c.selectionStart();
}

AttributeMap lineAttrs(const QTextBlock& b)
{
    return b.blockFormat().property(prop::BlockAttributes).toJsonObject();
}

} // namespace

class TestLoad : public QObject
{
    Q_OBJECT

    LoadResult load(QTextDocument& doc, const QString& fixture)
    {
        return DeltaDocument::load(&doc, loadFixtureDelta(fixture));
    }

private slots:
    void initTestCase() { QDir().mkpath(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR)); }

    // --- Generic checks over every fixture ------------------------------------

    void structureMatchesDelta_data()
    {
        QTest::addColumn<QString>("fixture");
        for (const QString& f : allFixtures())
            QTest::newRow(qPrintable(f)) << f;
    }

    void structureMatchesDelta()
    {
        QFETCH(QString, fixture);
        const Delta delta = loadFixtureDelta(fixture);
        QTextDocument doc;
        const LoadResult r = DeltaDocument::load(&doc, delta);
        QVERIFY2(r.ok, qPrintable(r.error));

        // Same length: Qt positions are Delta indices.
        QCOMPARE(qsizetype(doc.characterCount()), delta.length());

        // One block per line, each storing exactly its line attributes.
        QList<AttributeMap> lines;
        delta.eachLine([&](const Delta&, const AttributeMap& a, qsizetype) {
            lines << a;
            return true;
        });
        QCOMPARE(doc.blockCount(), int(lines.size()));
        int i = 0;
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next(), ++i)
            QCOMPARE(lineAttrs(b), lines.at(i));

        // Every character stores the attributes of its op; embeds store the embed.
        qsizetype pos = 0;
        for (const Op& op : delta.ops()) {
            if (op.isEmbed()) {
                const QTextCharFormat f = formatAt(doc, int(pos));
                QCOMPARE(doc.characterAt(int(pos)), QChar(QChar::ObjectReplacementCharacter));
                QCOMPARE(f.property(prop::Embed).toJsonObject(), op.embed());
                QCOMPARE(f.property(prop::InlineAttributes).toJsonObject(), op.attributes());
                pos += 1;
                continue;
            }
            for (qsizetype k = 0; k < op.text().size(); ++k, ++pos) {
                if (op.text().at(k) == QLatin1Char('\n'))
                    continue; // block separator: its attributes live on the block
                QCOMPARE(doc.characterAt(int(pos)), op.text().at(k));
                const AttributeMap stored = formatAt(doc, int(pos)).property(prop::InlineAttributes).toJsonObject();
                if (stored != op.attributes())
                    QFAIL(qPrintable(
                        QStringLiteral("position %1 (%2): stored %3, delta %4")
                            .arg(pos)
                            .arg(op.text().at(k))
                            .arg(QString::fromUtf8(QJsonDocument(stored).toJson(QJsonDocument::Compact)),
                                 QString::fromUtf8(QJsonDocument(op.attributes()).toJson(QJsonDocument::Compact)))));
            }
        }

        // Loading is not undoable and leaves the document unmodified.
        QVERIFY(!doc.isUndoAvailable());
        QVERIFY(!doc.isModified());

        // Renders without crashing; images saved for visual review.
        const QImage img = render(doc);
        QVERIFY(!img.isNull());
        QString out = fixture;
        out.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QStringLiteral(".json"), QStringLiteral(".png"));
        img.save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/load_") + out);
    }

    void warnings_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QStringList>("expected");
        for (const QString& f : allFixtures()) {
            if (!f.contains(QLatin1String("unknown_attributes")))
                QTest::newRow(qPrintable(f)) << f << QStringList();
        }
        QTest::newRow("unknown_attributes")
            << QStringLiteral("documents/unknown_attributes.json")
            << QStringList{QStringLiteral("line 1: unknown attribute \"x-line\" preserved"),
                           QStringLiteral("line 1: unknown attribute \"x-custom\" preserved"),
                           QStringLiteral("line 2: header: unsupported value 7"),
                           QStringLiteral("line 3: color: unsupported value \"not-a-color\""),
                           QStringLiteral("line 4: list: unsupported value \"weird\"")};
    }

    void warnings()
    {
        QFETCH(QString, fixture);
        QFETCH(QStringList, expected);
        QTextDocument doc;
        const LoadResult r = load(doc, fixture);
        QVERIFY(r.ok);
        QCOMPARE(r.warnings, expected);
    }

    // --- Specific mappings ------------------------------------------------------

    void inlineFormats()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/basic_inline.json").ok);
        const double base = doc.defaultFont().pointSizeF();
        const auto at = [&](const char* text) { return formatAt(doc, positionOf(doc, text)); };

        QCOMPARE(at("plain").fontWeight(), int(QFont::Normal));
        QCOMPARE(at("bold").fontWeight(), int(QFont::Bold));
        QVERIFY(at("italic").fontItalic());
        QVERIFY(at("under").fontUnderline());
        QVERIFY(at("strike").fontStrikeOut());
        QVERIFY(at("code").fontFixedPitch());
        QCOMPARE(at("code").background().color(), QColor(0xF5, 0xF5, 0xF5));
        QCOMPARE(at("code").fontPointSize(), base * 14 / 16);
        QCOMPARE(formatAt(doc, positionOf(doc, "H2O") + 1).verticalAlignment(), QTextCharFormat::AlignSubScript);
        QCOMPARE(formatAt(doc, positionOf(doc, "x2") + 1).verticalAlignment(), QTextCharFormat::AlignSuperScript);

        QVERIFY(at("link").isAnchor());
        QCOMPARE(at("link").anchorHref(), QStringLiteral("https://quilljs.com"));
        QVERIFY(at("link").fontUnderline());
        QCOMPARE(at("red").foreground().color(), QColor(0xF4, 0x43, 0x36));
        QCOMPARE(at("named").foreground().color(), QColor(0xF4, 0x43, 0x36)); // Flutter's red
        QCOMPARE(at("css").foreground().color().name(QColor::HexArgb), QStringLiteral("#80008000"));
        QCOMPARE(at("bg").background().color(), QColor(0xFF, 0xEB, 0x3B));
        QVERIFY(at("serif").fontFamilies().toStringList().contains(QStringLiteral("serif")));
        QVERIFY(at("mono").fontFixedPitch());
        QCOMPARE(at("Pacifico").fontFamilies().toStringList(), QStringList{QStringLiteral("Pacifico")});

        // flutter_quill sizes relative to its 16 base.
        QCOMPARE(at("small ").fontPointSize(), base * 10 / 16);
        QCOMPARE(at("large").fontPointSize(), base * 18 / 16);
        QCOMPARE(at("huge").fontPointSize(), base * 22 / 16);
        QCOMPARE(at("fourteen").fontPointSize(), base * 14 / 16);
        QCOMPARE(at("twenty").fontPointSize(), base * 20 / 16);
        QVERIFY(!at("cleared").hasProperty(QTextFormat::FontPointSize)); // "0" means no size
        QCOMPARE(at("smallflag").fontPointSize(), base * 12 / 16);

        // Explicit colour wins over the link colour.
        const QTextCharFormat all = at("everything");
        QCOMPARE(all.foreground().color(), QColor(0x21, 0x96, 0xF3));
        QVERIFY(all.isAnchor() && all.fontItalic() && all.fontUnderline());
        QCOMPARE(all.fontWeight(), int(QFont::Bold));
    }

    void headers()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/headers.json").ok);
        const double base = doc.defaultFont().pointSizeF();
        const double sizes[] = {34, 30, 24, 20, 18, 16};
        for (int level = 1; level <= 6; ++level) {
            const QTextBlock b = blockWithText(doc, QStringLiteral("Header %1").arg(level));
            QCOMPARE(b.blockFormat().headingLevel(), level);
            QCOMPARE(b.blockFormat().property(prop::BlockKind).toInt(), int(BlockKind::Header));
            const QTextCharFormat f = formatAt(doc, b.position());
            QCOMPARE(f.fontPointSize(), base * sizes[level - 1] / 16);
            QCOMPARE(f.fontWeight(), int(QFont::Bold));
            QCOMPARE(b.charFormat().fontPointSize(), base * sizes[level - 1] / 16);
        }
        // Inline formats inside a header keep the header size; inline size wins.
        QCOMPARE(formatAt(doc, positionOf(doc, "italic")).fontPointSize(), base * 34 / 16);
        QVERIFY(formatAt(doc, positionOf(doc, "italic")).fontItalic());
        QCOMPARE(formatAt(doc, positionOf(doc, "sized")).fontPointSize(), base * 22 / 16);
        QCOMPARE(blockWithText(doc, "Body text").blockFormat().headingLevel(), 0);
    }

    void nestedListNumbering()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/lists_nested.json").ok);
        const auto item = [&](const char* text) {
            const QTextBlock b = blockWithText(doc, text);
            QTextList* list = b.textList();
            if (!list)
                qFatal("not a list item: %s", text);
            return std::pair<QTextList*, QString>(list, list->itemText(b));
        };
        QCOMPARE(item("one").second, QStringLiteral("1."));
        QCOMPARE(item("two").second, QStringLiteral("2."));
        QCOMPARE(item("two.a").second, QStringLiteral("a."));
        QCOMPARE(item("two.b").second, QStringLiteral("b."));
        QCOMPARE(item("two.b.i").second, QStringLiteral("i."));
        QCOMPARE(item("two.b.ii").second, QStringLiteral("ii."));
        QCOMPARE(item("two.c").second, QStringLiteral("c."));
        QCOMPARE(item("three").second, QStringLiteral("3.")); // parent continues after nesting
        QCOMPARE(item("one").first, item("three").first);
        QCOMPARE(item("two.a").first, item("two.c").first);
        QCOMPARE(item("two.a").first->format().indent(), 2);
        QCOMPARE(item("two.b.i").first->format().indent(), 3);

        QCOMPARE(item("bullet a").first->format().style(), QTextListFormat::ListDisc);
        QCOMPARE(item("bullet b").first->format().style(), QTextListFormat::ListCircle);
        QCOMPARE(item("bullet c").first->format().style(), QTextListFormat::ListSquare);
        QCOMPARE(item("restart one").second, QStringLiteral("1.")); // new type at level 0 restarts
        QVERIFY(!blockWithText(doc, "paragraph").textList());
        QCOMPARE(item("again one").second, QStringLiteral("1.")); // paragraph ends the list
        QCOMPARE(item("again two").second, QStringLiteral("2."));
        QCOMPARE(blockWithText(doc, "one").blockFormat().property(prop::BlockKind).toInt(), int(BlockKind::ListItem));
    }

    void checklists()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/checklist.json").ok);
        using M = QTextBlockFormat::MarkerType;
        QCOMPARE(blockWithText(doc, "todo").blockFormat().marker(), M::Unchecked);
        QCOMPARE(blockWithText(doc, "done").blockFormat().marker(), M::Checked);
        QCOMPARE(blockWithText(doc, "nested done").blockFormat().marker(), M::Checked);
        // Checked and unchecked items share one list per level.
        QCOMPARE(blockWithText(doc, "todo").textList(), blockWithText(doc, "done").textList());
        QCOMPARE(blockWithText(doc, "todo").textList(), blockWithText(doc, "back").textList());
        QCOMPARE(blockWithText(doc, "nested todo").textList()->format().indent(), 2);
    }

    void blockquoteAndCode()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/blockquote_code.json").ok);
        const double base = doc.defaultFont().pointSizeF();
        const QTextBlock quote = blockWithText(doc, "A quote");
        QCOMPARE(quote.blockFormat().property(prop::BlockKind).toInt(), int(BlockKind::Blockquote));
        QCOMPARE(quote.blockFormat().leftMargin(), 16.0);
        QCOMPARE(formatAt(doc, quote.position()).foreground().color().alpha(), 153);
        QCOMPARE(formatAt(doc, positionOf(doc, "bold")).fontWeight(), int(QFont::Bold));

        const QTextBlock code = blockWithText(doc, "var x = 1;");
        QCOMPARE(code.blockFormat().property(prop::BlockKind).toInt(), int(BlockKind::CodeBlock));
        QVERIFY(formatAt(doc, code.position()).fontFixedPitch());
        QCOMPARE(formatAt(doc, code.position()).fontPointSize(), base * 13 / 16);
        QCOMPARE(lineAttrs(blockWithText(doc, "const y = 2;")).value("code-block").toString(),
                 QStringLiteral("javascript"));
        QCOMPARE(blockWithText(doc, "  return x;").text(), QStringLiteral("  return x;"));

        const QTextBlock indented = blockWithText(doc, "Indented quote");
        QCOMPARE(indented.blockFormat().indent(), 1);
        QCOMPARE(indented.blockFormat().leftMargin(), 16.0);
    }

    void alignmentDirectionIndent()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/align_rtl_indent.json").ok);
        QCOMPARE(blockWithText(doc, "left").blockFormat().alignment(), Qt::AlignLeft | Qt::AlignAbsolute);
        QCOMPARE(blockWithText(doc, "center").blockFormat().alignment(), Qt::AlignHCenter);
        QCOMPARE(blockWithText(doc, "right").blockFormat().alignment(), Qt::AlignRight | Qt::AlignAbsolute);
        QCOMPARE(doc.begin().next().next().next().blockFormat().alignment(), Qt::AlignJustify);
        const QTextBlock rtl = blockWithText(doc, QStringLiteral("שלום עולם"));
        QCOMPARE(rtl.blockFormat().layoutDirection(), Qt::RightToLeft);
        QVERIFY(!rtl.blockFormat().hasProperty(QTextFormat::BlockAlignment)); // leading = right in RTL
        QCOMPARE(blockWithText(doc, QStringLiteral("مرحبا")).blockFormat().alignment(),
                 Qt::AlignLeft | Qt::AlignAbsolute);
        QCOMPARE(blockWithText(doc, "indent one").blockFormat().indent(), 1);
        QCOMPARE(blockWithText(doc, "indent two").blockFormat().indent(), 2);
        QCOMPARE(doc.indentWidth(), 24.0);
        const QTextBlockFormat lh = blockWithText(doc, "line height 1.5").blockFormat();
        QCOMPARE(lh.lineHeightType(), int(QTextBlockFormat::ProportionalHeight));
        QCOMPARE(lh.lineHeight(), 150.0);
        QCOMPARE(blockWithText(doc, "line height 1.15").blockFormat().lineHeight(), 115.0);
    }

    void embeds()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/embeds.json").ok);

        // data: image is decoded and sized from width/height attributes.
        const int imgPos = positionOf(doc, "Image: ") + 7;
        const QTextCharFormat img = formatAt(doc, imgPos);
        QVERIFY(img.isImageFormat());
        QCOMPARE(img.toImageFormat().width(), 40.0);
        QCOMPARE(img.toImageFormat().height(), 30.0);
        QCOMPARE(img.property(prop::EmbedType).toString(), QStringLiteral("image"));
        const QVariant res = doc.resource(QTextDocument::ImageResource, QUrl(img.toImageFormat().name()));
        QVERIFY(res.isValid());
        QCOMPARE(res.value<QImage>().size(), QSize(2, 2));

        // The CSS style wins over width/height attributes (flutter_quill_extensions).
        const QTextBlock second = doc.begin().next();
        const QTextImageFormat sized = formatAt(doc, second.position()).toImageFormat();
        QCOMPARE(sized.width(), 200.0);
        QCOMPARE(sized.height(), 50.0);

        // Other embeds: object replacement char with the embed stored.
        const QTextBlock video = second.next();
        const QTextCharFormat v = formatAt(doc, video.position());
        QCOMPARE(v.objectType(), int(prop::EmbedObjectType));
        QCOMPARE(v.property(prop::EmbedType).toString(), QStringLiteral("video"));
        const QTextCharFormat mention = formatAt(doc, video.next().next().next().position());
        QCOMPARE(mention.property(prop::EmbedType).toString(), QStringLiteral("mention"));
        QCOMPARE(mention.fontWeight(), int(QFont::Bold));
    }

    void unknownAttributesArePreservedWithoutEffect()
    {
        QTextDocument doc;
        QVERIFY(load(doc, "documents/unknown_attributes.json").ok);
        QCOMPARE(blockWithText(doc, "bad header").blockFormat().headingLevel(), 0);
        QVERIFY(!blockWithText(doc, "bad list").textList());
        // Inline attributes on a newline style the empty-line typing format.
        QCOMPARE(blockWithText(doc, "inline attr on newline").charFormat().fontWeight(), int(QFont::Bold));
    }

    void missingTrailingNewlineIsAppended()
    {
        QTextDocument doc;
        const LoadResult r = DeltaDocument::load(&doc, Delta().insert("abc"));
        QVERIFY(r.ok);
        QCOMPARE(r.warnings.size(), 1);
        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
        QCOMPARE(doc.characterCount(), 4);
    }

    void emptyDeltaGivesEmptyDocument()
    {
        QTextDocument doc;
        doc.setPlainText("old");
        const LoadResult r = DeltaDocument::load(&doc, Delta());
        QVERIFY(r.ok);
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(doc.characterCount(), 1);
    }

    void changeDeltaIsRejected()
    {
        QTextDocument doc;
        doc.setPlainText("keep");
        const LoadResult r = DeltaDocument::load(&doc, Delta().retain(1).insert("x"));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(doc.toPlainText(), QStringLiteral("keep"));
    }

    void loadReplacesContentAndKeepsUndoSetting()
    {
        QTextDocument doc;
        doc.setPlainText("old content");
        QVERIFY(doc.isUndoRedoEnabled());
        QVERIFY(DeltaDocument::load(&doc, testutil::delta(R"([{"insert":"new\n"}])")).ok);
        QCOMPARE(doc.toPlainText(), QStringLiteral("new"));
        QVERIFY(doc.isUndoRedoEnabled());
    }

    void customStyleOptions()
    {
        QTextDocument doc;
        LoadOptions o;
        o.style.baseFont = QFont(QStringLiteral("Sans"), 20);
        o.style.colorDialect = ColorDialect::Css;
        o.style.indentWidth = 40;
        QVERIFY(
            DeltaDocument::load(
                &doc,
                testutil::delta(
                    R"([{"insert":"r","attributes":{"color":"red","size":"8"}},{"insert":"\n","attributes":{"indent":1}}])"),
                o)
                .ok);
        const QTextCharFormat f = formatAt(doc, 0);
        QCOMPARE(f.foreground().color(), QColor(255, 0, 0)); // CSS red
        QCOMPARE(f.fontPointSize(), 10.0);                   // 8/16 of 20pt
        QCOMPARE(doc.indentWidth(), 40.0);
    }

    void largeDocumentPerformance()
    {
        Delta d;
        for (int i = 0; i < 5000; ++i) {
            d.insert(QStringLiteral("Line %1 with ").arg(i)).insert("bold", {{"bold", true}}).insert(" text");
            d.insert("\n", i % 3 ? AttributeMap{{"list", "ordered"}} : AttributeMap{});
        }
        QTextDocument doc;
        QElapsedTimer t;
        t.start();
        QVERIFY(DeltaDocument::load(&doc, d).ok);
        qInfo("loaded 5000 lines in %lld ms", qlonglong(t.elapsed()));
        QCOMPARE(doc.blockCount(), 5000);
        QVERIFY(t.elapsed() < 5000);
    }

    void genericFontNamesWorkEverywhere()
    {
        // Quill's generic names have no family of that name on Windows; the
        // formats carry a style hint and families present on each platform.
        const StyleOptions style;
        const auto fmt = [&](const char* font) {
            return formats::charFormat(AttributeMap{{QStringLiteral("font"), QString::fromLatin1(font)}}, {}, style);
        };
        const QTextCharFormat mono = fmt("monospace");
        QCOMPARE(mono.fontStyleHint(), QFont::Monospace);
        QVERIFY(mono.fontFixedPitch());
        const QStringList monoFamilies = mono.fontFamilies().toStringList();
        for (const char* family : {"Consolas", "Courier New", "Menlo", "DejaVu Sans Mono"})
            QVERIFY2(monoFamilies.contains(QString::fromLatin1(family)), family);
        QVERIFY(monoFamilies.contains(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
        // Resolving needs a font database; Qt's offscreen platform on Windows
        // has none. With the windows platform, the style hint does not help
        // when no listed family exists: the family list must cover Windows.
        if (!QFontDatabase::families().isEmpty())
            QVERIFY2(QFontInfo(mono.font()).fixedPitch(), qPrintable(QFontInfo(mono.font()).family()));

        const QTextCharFormat serif = fmt("serif");
        QCOMPARE(serif.fontStyleHint(), QFont::Serif);
        for (const char* family : {"Times New Roman", "Times"})
            QVERIFY2(serif.fontFamilies().toStringList().contains(QString::fromLatin1(family)), family);
        const QTextCharFormat sans = fmt("sans-serif");
        QCOMPARE(sans.fontStyleHint(), QFont::SansSerif);
        for (const char* family : {"Arial", "Helvetica"})
            QVERIFY2(sans.fontFamilies().toStringList().contains(QString::fromLatin1(family)), family);

        // Export writes the Quill name back, whatever family was used.
        QTextDocument doc;
        const Delta input = testutil::delta(
            "[{'insert':'a','attributes':{'font':'monospace'}},{'insert':'b','attributes':{'font':'serif'}},"
            "{'insert':'c','attributes':{'font':'sans-serif'}},{'insert':'\\n'}]");
        DeltaDocument::load(&doc, input);
        QCOMPARE(DeltaDocument::toDelta(&doc), input);
    }
};

QTEST_MAIN(TestLoad)
#include "tst_load.moc"
