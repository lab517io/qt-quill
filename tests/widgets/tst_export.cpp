// QTextDocument -> Delta export and round-trip (milestone M4).
#include "TestUtil.h"

#include <quill/DeltaDocument.h>

#include <QDir>
#include <QRandomGenerator>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextTable>

using namespace quill;
using testutil::delta;

// Test JSON is written with single quotes ('insert') and converted by
// testutil::delta: Qt 6.4's moc cannot tokenize raw string literals that
// contain escapes such as "\n", and loses the test class.

namespace {

Delta fixture(const QString& name)
{
    const QJsonDocument json = testutil::loadJson(name);
    const auto d = Delta::fromJson(json.isArray() ? QJsonValue(json.array()) : QJsonValue(json.object()));
    if (!d)
        qFatal("bad fixture %s", qPrintable(name));
    return *d;
}

/// The canonical (merged) form of a document Delta, as push() would build it.
Delta canonical(const Delta& d)
{
    Delta out;
    for (const Op& op : d.ops())
        out.push(op);
    return out;
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

/// Selects the first occurrence of `text`.
QTextCursor select(QTextDocument& doc, const QString& text)
{
    QTextCursor c = doc.find(text);
    if (c.isNull())
        qFatal("text not found: %s", qPrintable(text));
    return c;
}

QTextBlock blockOf(QTextDocument& doc, const QString& text) { return select(doc, text).block(); }

/// Loads `json`, applies `edit`, exports.
template <class F>
Delta edited(const char* json, F edit)
{
    QTextDocument doc;
    if (!DeltaDocument::load(&doc, delta(json)).ok)
        qFatal("load failed");
    edit(doc);
    return DeltaDocument::toDelta(&doc);
}

/// QCOMPARE for Deltas built inline, reporting the caller's line.
void check(const Delta& actual, const Delta& expectedIn, int line)
{
    const Delta expected = canonical(expectedIn);
    if (actual != expected) // QTest truncates long values; show both in full
        qWarning("line %d\n  actual:   %s\n  expected: %s", line, actual.toJson().constData(),
                 expected.toJson().constData());
    QTest::qCompare(actual, expected, "actual", "expected", __FILE__, line);
}

} // namespace

class TestExport : public QObject
{
    Q_OBJECT

private slots:
    // --- Round trip ---------------------------------------------------------------

    void roundTrip_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString& f : allFixtures())
            QTest::newRow(qPrintable(f)) << f;
    }

    void roundTrip()
    {
        QFETCH(QString, name);
        const Delta original = fixture(name);
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, original).ok);
        QStringList warnings;
        const Delta exported = DeltaDocument::toDelta(&doc, {}, &warnings);
        QCOMPARE(exported, canonical(original));
        QVERIFY(warnings.isEmpty());
        QCOMPARE(exported.length(), qsizetype(doc.characterCount()));
    }

    void roundTripWithCustomStyle()
    {
        // The style used for loading is remembered by the document.
        LoadOptions o;
        o.style.baseFont = QFont(QStringLiteral("Serif"), 23);
        o.style.colorDialect = ColorDialect::Css;
        const Delta original = fixture("documents/basic_inline.json");
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, original, o).ok);
        QCOMPARE(DeltaDocument::toDelta(&doc), canonical(original));
    }

    void subRangeEqualsSlice_data()
    {
        roundTrip_data();
    }

    void subRangeEqualsSlice()
    {
        QFETCH(QString, name);
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, fixture(name)).ok);
        const Delta full = DeltaDocument::toDelta(&doc);
        const int n = doc.characterCount();
        QRandomGenerator rng(42);
        for (int i = 0; i < 300; ++i) {
            int a = int(rng.bounded(n + 1));
            int b = int(rng.bounded(n + 1));
            if (a > b)
                std::swap(a, b);
            const Delta part = DeltaDocument::toDelta(&doc, a, b);
            if (part != canonical(full.slice(a, b)))
                QFAIL(qPrintable(QStringLiteral("range [%1, %2): %3 vs %4")
                                     .arg(a)
                                     .arg(b)
                                     .arg(QString::fromUtf8(part.toJson()),
                                          QString::fromUtf8(canonical(full.slice(a, b)).toJson()))));
        }
        QCOMPARE(DeltaDocument::toDelta(&doc, 0, 0), Delta());
        QCOMPARE(DeltaDocument::toDelta(&doc, -5, n + 5), full);
    }

    void exportIsStableAfterRandomQtEdits_data() { roundTrip_data(); }

    void exportIsStableAfterRandomQtEdits()
    {
        // After arbitrary edits made with plain QTextCursor calls, the export
        // must be a fixed point: export -> load -> export gives the same Delta.
        // This catches loader styling leaking into attributes and attributes
        // lost on reload.
        QFETCH(QString, name);
        const Delta original = fixture(name);
        QRandomGenerator rng(7);
        int changed = 0;
        for (int iteration = 0; iteration < 25; ++iteration) {
            QTextDocument doc;
            QVERIFY(DeltaDocument::load(&doc, original).ok);
            const double base = doc.defaultFont().pointSizeF();
            QStringList log;
            for (int step = 0; step < 12; ++step) {
                const int n = doc.characterCount() - 1; // last position before the terminator
                int a = int(rng.bounded(n + 1));
                int b = int(rng.bounded(n + 1));
                if (a > b)
                    std::swap(a, b);
                QTextCursor c(&doc);
                c.setPosition(a);
                c.setPosition(b, QTextCursor::KeepAnchor);
                QTextCharFormat cf;
                QTextBlockFormat bf = c.blockFormat();
                const int kind = int(rng.bounded(11));
                log << QStringLiteral("%1[%2,%3)").arg(kind).arg(a).arg(b);
                switch (kind) {
                case 0:
                    cf.setFontWeight(rng.bounded(2) ? QFont::Bold : QFont::Normal);
                    cf.setFontItalic(rng.bounded(2));
                    c.mergeCharFormat(cf);
                    break;
                case 1:
                    cf.setForeground(QColor::fromRgb(rng.bounded(0x1000000u)));
                    c.mergeCharFormat(cf);
                    break;
                case 2: {
                    const double sizes[] = {base * 10 / 16, base * 18 / 16, base * 1.3};
                    cf.setFontPointSize(sizes[rng.bounded(3)]);
                    c.mergeCharFormat(cf);
                    break;
                }
                case 3:
                    c.clearSelection();
                    c.insertText(QStringLiteral("xy"));
                    break;
                case 4:
                    c.removeSelectedText();
                    break;
                case 5:
                    c.clearSelection();
                    c.insertBlock();
                    break;
                case 6:
                    bf.setHeadingLevel(int(rng.bounded(4)));
                    c.setBlockFormat(bf);
                    break;
                case 7:
                    if (QTextList* l = c.block().textList())
                        l->remove(c.block());
                    else
                        c.createList(rng.bounded(2) ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
                    break;
                case 8: {
                    const Qt::Alignment aligns[] = {Qt::AlignHCenter, Qt::AlignRight | Qt::AlignAbsolute,
                                                    Qt::AlignJustify, Qt::AlignLeft};
                    bf.setAlignment(aligns[rng.bounded(4)]);
                    c.setBlockFormat(bf);
                    break;
                }
                case 9:
                    cf.setAnchor(true);
                    cf.setAnchorHref(QStringLiteral("https://example.org/%1").arg(step));
                    c.mergeCharFormat(cf);
                    break;
                case 10: {
                    QTextCharFormat f = c.charFormat();
                    f.clearForeground();
                    c.setCharFormat(f);
                    break;
                }
                }
            }
            const Delta first = DeltaDocument::toDelta(&doc);
            if (first != canonical(original))
                ++changed;
            QVERIFY(first.isDocument());
            QCOMPARE(first.length(), qsizetype(doc.characterCount()));
            QTextDocument reloaded;
            QVERIFY(DeltaDocument::load(&reloaded, first).ok);
            const Delta second = DeltaDocument::toDelta(&reloaded);
            if (first != second)
                QFAIL(qPrintable(QStringLiteral("iteration %1, edits %2\nfirst:  %3\nsecond: %4")
                                     .arg(iteration)
                                     .arg(log.join(QLatin1Char(' ')),
                                          QString::fromUtf8(first.toJson()),
                                          QString::fromUtf8(second.toJson()))));
        }
        QVERIFY2(changed >= 20, qPrintable(QStringLiteral("only %1 of 25 iterations changed the document").arg(changed)));
    }

    // --- Documents built or edited with plain Qt APIs --------------------------------

    void emptyDocument()
    {
        QTextDocument doc;
        check(DeltaDocument::toDelta(&doc), delta("[{'insert':'\\n'}]"), __LINE__);
    }

    void plainTextDocument()
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("one\ntwo"));
        check(DeltaDocument::toDelta(&doc), delta("[{'insert':'one\\ntwo\\n'}]"), __LINE__);
    }

    void inlineEdits()
    {
        const char* json = "[{'insert':'plain bold red\\n'}]";
        check(edited(json, [](QTextDocument& d) {
                     QTextCharFormat f;
                     f.setFontWeight(QFont::Bold);
                     select(d, "bold").mergeCharFormat(f);
                 }), delta("[{'insert':'plain '},{'insert':'bold','attributes':{'bold':true}},{'insert':' red\\n'}]"), __LINE__);

        check(edited(json, [](QTextDocument& d) {
                     QTextCharFormat f;
                     f.setFontItalic(true);
                     f.setFontUnderline(true);
                     f.setFontStrikeOut(true);
                     f.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
                     select(d, "plain").mergeCharFormat(f);
                 }), delta("[{'insert':'plain','attributes':{'italic':true,'script':'super','strike':true,'underline':true}},{'insert':' bold red\\n'}]"), __LINE__);

        check(edited(json, [](QTextDocument& d) {
                     QTextCharFormat f;
                     f.setForeground(QColor(1, 2, 3));
                     f.setBackground(QColor(255, 255, 0));
                     select(d, "red").mergeCharFormat(f);
                 }), delta("[{'insert':'plain bold '},{'insert':'red','attributes':{'background':'#FFFFFF00','color':'#FF010203'}},{'insert':'\\n'}]"), __LINE__);

        check(edited(json, [](QTextDocument& d) {
                     QTextCharFormat f;
                     f.setAnchor(true);
                     f.setAnchorHref(QStringLiteral("https://qt.io"));
                     select(d, "bold").mergeCharFormat(f);
                 }), delta("[{'insert':'plain '},{'insert':'bold','attributes':{'link':'https://qt.io'}},{'insert':' red\\n'}]"), __LINE__);

        check(edited(json, [](QTextDocument& d) {
                     QTextCharFormat f;
                     f.setFontFamilies({QStringLiteral("Arial")});
                     select(d, "plain").mergeCharFormat(f);
                 }), delta("[{'insert':'plain','attributes':{'font':'Arial'}},{'insert':' bold red\\n'}]"), __LINE__);
    }

    void sizeEdits()
    {
        QTextDocument probe;
        const double base = probe.defaultFont().pointSizeF();
        const char* json = "[{'insert':'aa bb cc\\n'}]";
        const Delta d = edited(json, [&](QTextDocument& doc) {
            QTextCharFormat large, fourteen;
            large.setFontPointSize(base * 18 / 16);
            fourteen.setFontPointSize(base * 14 / 16);
            select(doc, "aa").mergeCharFormat(large);
            select(doc, "cc").mergeCharFormat(fourteen);
        });
        check(d, delta("[{'insert':'aa','attributes':{'size':'large'}},{'insert':' bb '},{'insert':'cc','attributes':{'size':'14'}},{'insert':'\\n'}]"), __LINE__);
    }

    void removingFormats()
    {
        const char* json =
            "[{'insert':'bold','attributes':{'bold':true,'color':'red','size':'huge'}},{'insert':'\\n'}]";
        const Delta d = edited(json, [](QTextDocument& doc) {
            QTextCursor c = select(doc, "bold");
            QTextCharFormat f = c.charFormat();
            f.setFontWeight(QFont::Normal);
            f.clearForeground();
            f.clearProperty(QTextFormat::FontPointSize);
            c.setCharFormat(f);
        });
        check(d, delta("[{'insert':'bold\\n'}]"), __LINE__);
    }

    void untouchedValuesKeepTheirSpelling()
    {
        // Flutter name "red", numeric-string size "15.0" and an unknown key
        // survive an unrelated edit on the same text.
        const char* json =
            "[{'insert':'word','attributes':{'color':'red','size':'15.0','x-tag':1}},{'insert':'\\n'}]";
        const Delta d = edited(json, [](QTextDocument& doc) {
            QTextCharFormat f;
            f.setFontItalic(true);
            select(doc, "word").mergeCharFormat(f);
        });
        check(d, delta("[{'insert':'word','attributes':{'color':'red','italic':true,'size':'15.0','x-tag':1}},{'insert':'\\n'}]"), __LINE__);
    }

    void loaderStylingDoesNotLeak()
    {
        // Header bold/size, code background, link colour/underline, quote
        // colour are styling, not attributes.
        const char* json = "[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}},"
                           "{'insert':'x','attributes':{'code':true}},{'insert':' '},"
                           "{'insert':'link','attributes':{'link':'https://a.b'}},{'insert':'\\n'},"
                           "{'insert':'q'},{'insert':'\\n','attributes':{'blockquote':true}}]";
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, delta(json)).ok);
        QCOMPARE(DeltaDocument::toDelta(&doc), canonical(delta(json)));
    }

    void typingInheritsAttributes()
    {
        const Delta d = edited("[{'insert':'ab','attributes':{'bold':true,'x-tag':'t'}},{'insert':'\\n'}]",
                               [](QTextDocument& doc) {
                                   QTextCursor c(&doc);
                                   c.setPosition(1);
                                   c.insertText(QStringLiteral("X")); // takes the format before the cursor
                               });
        check(d, delta("[{'insert':'aXb','attributes':{'bold':true,'x-tag':'t'}},{'insert':'\\n'}]"), __LINE__);
    }

    void blockEdits()
    {
        const char* json = "[{'insert':'one\\ntwo\\nthree\\nfour\\n'}]";
        const Delta d = edited(json, [](QTextDocument& doc) {
            QTextCursor c(blockOf(doc, "one"));
            QTextBlockFormat bf = c.blockFormat();
            bf.setHeadingLevel(2);
            bf.setAlignment(Qt::AlignHCenter);
            c.setBlockFormat(bf);

            QTextCursor two(blockOf(doc, "two"));
            QTextListFormat lf;
            lf.setStyle(QTextListFormat::ListDecimal);
            lf.setIndent(2);
            two.createList(lf);

            QTextCursor three(blockOf(doc, "three"));
            QTextBlockFormat tf = three.blockFormat();
            tf.setLayoutDirection(Qt::RightToLeft);
            tf.setIndent(2);
            tf.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
            three.setBlockFormat(tf);

            QTextCursor four(blockOf(doc, "four"));
            four.createList(QTextListFormat::ListDisc);
            QTextBlockFormat ff = four.blockFormat();
            ff.setMarker(QTextBlockFormat::MarkerType::Checked);
            four.setBlockFormat(ff);
        });
        check(d, delta("[{'insert':'one'},{'insert':'\\n','attributes':{'align':'center','header':2}},"
                          "{'insert':'two'},{'insert':'\\n','attributes':{'indent':1,'list':'ordered'}},"
                          "{'insert':'three'},{'insert':'\\n','attributes':{'direction':'rtl','indent':2,'line-height':1.5}},"
                          "{'insert':'four'},{'insert':'\\n','attributes':{'list':'checked'}}]"), __LINE__);
    }

    void removingBlockFormats()
    {
        const char* json = "[{'insert':'h'},{'insert':'\\n','attributes':{'header':1,'align':'right'}},"
                           "{'insert':'li'},{'insert':'\\n','attributes':{'list':'bullet'}}]";
        const Delta d = edited(json, [](QTextDocument& doc) {
            QTextCursor h(blockOf(doc, "h"));
            QTextBlockFormat bf = h.blockFormat();
            bf.setHeadingLevel(0);
            bf.clearProperty(QTextFormat::BlockAlignment);
            h.setBlockFormat(bf);
            QTextBlock li = blockOf(doc, "li");
            li.textList()->remove(li);
        });
        check(d, delta("[{'insert':'h\\nli\\n'}]"), __LINE__);
    }

    void splittingALineKeepsItsAttributes()
    {
        // Enter in the middle of a header: both halves stay headers, as in Quill.
        const Delta d = edited("[{'insert':'Header'},{'insert':'\\n','attributes':{'header':2}}]",
                               [](QTextDocument& doc) {
                                   QTextCursor c(&doc);
                                   c.setPosition(3);
                                   c.insertBlock();
                               });
        check(d, delta("[{'insert':'Hea'},{'insert':'\\n','attributes':{'header':2}},"
                          "{'insert':'der'},{'insert':'\\n','attributes':{'header':2}}]"), __LINE__);
    }

    void softLineBreakBecomesALine()
    {
        const Delta d = edited("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}}]",
                               [](QTextDocument& doc) {
                                   QTextCursor c(&doc);
                                   c.setPosition(1);
                                   c.insertText(QString(QChar::LineSeparator));
                               });
        check(d, delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}},"
                          "{'insert':'b'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        QCOMPARE(d.length(), qsizetype(4)); // positions stay aligned
    }

    void imagesInsertedWithQt()
    {
        const Delta d = edited("[{'insert':'\\n'}]", [](QTextDocument& doc) {
            QTextCursor c(&doc);
            QTextImageFormat img;
            img.setName(QStringLiteral("pic.png"));
            img.setWidth(50);
            c.insertImage(img);
        });
        check(d, delta("[{'insert':{'image':'pic.png'},'attributes':{'width':'50'}},{'insert':'\\n'}]"), __LINE__);
    }

    void resizedImageUpdatesAttributes()
    {
        // style "width:200px" wins on load; after a resize, the new width is
        // written and removed from style so it is not overridden on reload.
        const char* json = "[{'insert':{'image':'a.png'},'attributes':{'width':'100','style':'width:200px; margin: 4px;'}},{'insert':'\\n'}]";
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, delta(json)).ok);
        QTextCursor c(&doc);
        c.setPosition(0);
        c.setPosition(1, QTextCursor::KeepAnchor);
        QTextImageFormat img = c.charFormat().toImageFormat();
        img.setWidth(320);
        c.setCharFormat(img);
        const Delta d = DeltaDocument::toDelta(&doc);
        check(d, delta("[{'insert':{'image':'a.png'},'attributes':{'style':'margin: 4px;','width':'320'}},{'insert':'\\n'}]"), __LINE__);

        QTextDocument reloaded;
        QVERIFY(DeltaDocument::load(&reloaded, d).ok);
        QTextCursor r(&reloaded);
        r.setPosition(1);
        QCOMPARE(r.charFormat().toImageFormat().width(), 320.0);
    }

    void htmlImport()
    {
        // Content pasted as HTML gets Quill attributes (paste path in M5).
        QTextDocument doc;
        doc.setHtml(QStringLiteral("<h1>Title</h1><p><b>bold</b> and <i>it</i> "
                                   "<a href=\"https://x.y\">link</a></p>"
                                   "<ul><li>first</li><li>second</li></ul><ol><li>num</li></ol>"
                                   "<p align=\"center\">mid</p>"));
        check(DeltaDocument::toDelta(&doc), delta("[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}},"
                       "{'insert':'bold','attributes':{'bold':true}},{'insert':' and '},"
                       "{'insert':'it','attributes':{'italic':true}},{'insert':' '},"
                       "{'insert':'link','attributes':{'color':'#FF0000FF','link':'https://x.y'}},{'insert':'\\n'},"
                       "{'insert':'first'},{'insert':'\\n','attributes':{'list':'bullet'}},"
                       "{'insert':'second'},{'insert':'\\n','attributes':{'list':'bullet'}},"
                       "{'insert':'num'},{'insert':'\\n','attributes':{'list':'ordered'}},"
                       "{'insert':'mid'},{'insert':'\\n','attributes':{'align':'center'}}]"), __LINE__);
    }

    void tablesAreFlattenedWithWarning()
    {
        QTextDocument doc;
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("before"));
        QTextTable* table = c.insertTable(1, 2);
        table->cellAt(0, 0).firstCursorPosition().insertText(QStringLiteral("A"));
        table->cellAt(0, 1).firstCursorPosition().insertText(QStringLiteral("B"));
        QStringList warnings;
        const Delta d = DeltaDocument::toDelta(&doc, {}, &warnings);
        QCOMPARE(warnings.size(), 1);
        check(d, delta("[{'insert':'before\\nA\\nB\\n\\n'}]"), __LINE__);
    }

    void exportIsFast()
    {
        Delta d;
        for (int i = 0; i < 5000; ++i) {
            d.insert(QStringLiteral("Line %1 with ").arg(i)).insert("bold", {{"bold", true}}).insert(" text");
            d.insert("\n", i % 3 ? AttributeMap{{"list", "ordered"}} : AttributeMap{});
        }
        QTextDocument doc;
        QVERIFY(DeltaDocument::load(&doc, d).ok);
        QElapsedTimer t;
        t.start();
        const Delta back = DeltaDocument::toDelta(&doc);
        qInfo("exported 5000 lines in %lld ms", qlonglong(t.elapsed()));
        QCOMPARE(back, canonical(d));
        QVERIFY(t.elapsed() < 5000);
    }
};

QTEST_MAIN(TestExport)
#include "tst_export.moc"
