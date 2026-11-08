// M1 smoke test. Confirms the toolchain, headless widgets, and the Qt facts
// later milestones depend on.
#include <quill/Version.h>

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextImageFormat>
#include <QTextList>

namespace {

QImage render(QTextDocument& doc)
{
    doc.setTextWidth(300);
    QImage img(300, int(doc.size().height()) + 1, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    doc.drawContents(&p);
    return img;
}

QTextDocument* makeListDocument(QTextBlockFormat::MarkerType marker, QObject* parent)
{
    auto* doc = new QTextDocument(parent);
    QTextCursor c(doc);
    c.insertText(QStringLiteral("task"));
    c.createList(QTextListFormat::ListDisc);
    QTextBlockFormat bf = c.blockFormat();
    bf.setMarker(marker);
    c.setBlockFormat(bf);
    return doc;
}

} // namespace

class TestSmoke : public QObject
{
    Q_OBJECT

private slots:
    void libraryLinks()
    {
        QCOMPARE(quill::version(), QStringLiteral("0.1.0"));
        QVERIFY(QString::fromLatin1(qVersion()).startsWith(QStringLiteral("6.")));
    }

    // FACT for M3-M5: QTextDocument positions equal Delta indices.
    // Delta document  "ab\nc" + {image} + "\n"  has length 6.
    // Qt document: a, b, block separator, c, image (U+FFFC), final block
    // terminator -> characterCount() == 6. The final terminator plays the
    // role of the Delta's mandatory trailing "\n"; each block separator is
    // the "\n" that carries the previous line's attributes.
    void documentPositionsMatchDelta()
    {
        QTextDocument doc;
        QTextCursor c(&doc);
        c.insertText(QStringLiteral("ab"));
        c.insertBlock();
        c.insertText(QStringLiteral("c"));
        QTextImageFormat img;
        img.setName(QStringLiteral("data:image/png;base64,"));
        c.insertImage(img);

        QCOMPARE(doc.characterCount(), 6);
        QCOMPARE(doc.characterAt(2), QChar(QChar::ParagraphSeparator));
        QCOMPARE(doc.characterAt(4), QChar(QChar::ObjectReplacementCharacter));
        QCOMPARE(doc.characterAt(5), QChar(QChar::ParagraphSeparator));
        QCOMPARE(doc.findBlock(3).position(), 3);
        QCOMPARE(doc.blockCount(), 2);
    }

    void textEditWorksHeadless()
    {
        QTextEdit edit;
        edit.resize(400, 300);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        edit.setFocus();
        QTest::keyClicks(&edit, QStringLiteral("hello"));
        QTest::keyClick(&edit, Qt::Key_Return);
        QTest::keyClicks(&edit, QStringLiteral("world"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\nworld"));
        QCOMPARE(edit.document()->blockCount(), 2);
    }

    // FACT for M3/M5: checklist markers (Qt >= 5.14) render, and checked,
    // unchecked and plain bullets all look different.
    void checklistMarkersRender()
    {
        QObject owner;
        const QImage none = render(*makeListDocument(QTextBlockFormat::MarkerType::NoMarker, &owner));
        const QImage unchecked = render(*makeListDocument(QTextBlockFormat::MarkerType::Unchecked, &owner));
        const QImage checked = render(*makeListDocument(QTextBlockFormat::MarkerType::Checked, &owner));
        QCOMPARE(none.size(), unchecked.size());
        QVERIFY(none != unchecked);
        QVERIFY(unchecked != checked);
        QVERIFY(none != checked);
    }

    // FACT for M5: does QTextEdit toggle a checklist marker on mouse click by
    // itself? Recorded, not asserted; M5 decides whether to override.
    void probeBuiltInCheckboxToggle()
    {
        QTextEdit edit;
        edit.resize(400, 300);
        QTextCursor c(edit.document());
        c.insertText(QStringLiteral("task"));
        c.createList(QTextListFormat::ListDisc);
        QTextBlockFormat bf = c.blockFormat();
        bf.setMarker(QTextBlockFormat::MarkerType::Unchecked);
        c.setBlockFormat(bf);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));

        const QTextBlock block = edit.document()->firstBlock();
        const QRectF br = edit.document()->documentLayout()->blockBoundingRect(block);
        const QTextLine line = block.layout()->lineAt(0);
        const qreal textX = br.left() + line.x();
        // The marker sits in the list indent, left of the text start.
        bool toggled = false;
        for (qreal dx = 4; dx <= textX && !toggled; dx += 2) {
            const QPoint pt(int(textX - dx), int(br.top() + line.height() / 2));
            QTest::mouseClick(edit.viewport(), Qt::LeftButton, {}, pt);
            toggled = edit.document()->firstBlock().blockFormat().marker()
                      == QTextBlockFormat::MarkerType::Checked;
        }
        qInfo("Qt %s: built-in checkbox toggle on click: %s", qVersion(),
              toggled ? "YES" : "NO");
    }
};

QTEST_MAIN(TestSmoke)
#include "tst_smoke.moc"
