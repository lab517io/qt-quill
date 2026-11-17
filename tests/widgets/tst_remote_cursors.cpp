// RemoteCursors: peer cursors follow edits and are painted where they are.
#include "EditorTestUtil.h"

#include <quill/RemoteCursors.h>

#include <QTest>

using quill::Delta;
using quill::Range;
using quill::RemoteCursors;
using quill::Source;
using testutil::delta;

class TestRemoteCursors : public QObject
{
    Q_OBJECT

    std::unique_ptr<quill::QuillEditor> e;

private slots:
    void init()
    {
        e = testutil::makeEditor();
        e->setContents(Delta().insert("The lazy fox jumps\nover the dog\n"));
    }

    void cleanup() { e.reset(); }

    void followsEdits()
    {
        RemoteCursors* rc = e->remoteCursors();
        QCOMPARE(e->remoteCursors(), rc);
        rc->setCursor("ann", "Ann", Qt::red, {4, 4});
        rc->setCursor("bob", "Bob", Qt::blue, {13, 0});
        e->insertText(0, "Oh, ", {}, Source::User);
        QCOMPARE(rc->cursor("ann")->range, (Range{8, 4}));
        QCOMPARE(rc->cursor("bob")->range, (Range{17, 0}));
        // Typing at a peer's caret pushes it along.
        e->insertText(17, "x", {}, Source::User);
        QCOMPARE(rc->cursor("bob")->range, (Range{18, 0}));
        // A delete over a selection shrinks it.
        e->deleteText(6, 4, Source::Api);
        QCOMPARE(rc->cursor("ann")->range, (Range{6, 2}));
        QCOMPARE(rc->cursor("bob")->range, (Range{14, 0}));
        // Clamped to the document.
        rc->setCursor("ann", "Ann", Qt::red, {500, 9});
        QCOMPARE(rc->cursor("ann")->range, (Range{e->getLength() - 1, 0}));
        rc->removeCursor("ann");
        QVERIFY(!rc->cursor("ann"));
        QCOMPARE(rc->cursors().size(), 1);
        rc->clear();
        QVERIFY(rc->cursors().isEmpty());
    }

    void painted()
    {
        RemoteCursors* rc = e->remoteCursors();
        const QColor caretColor(220, 0, 120);
        const QColor selColor(0, 160, 0);
        rc->setCursor("ann", "Ann", caretColor, {3, 0}); // after "The"
        rc->setCursor("bob", "", selColor, {19, 4});     // "over" on line 2
        QCoreApplication::processEvents();
        const QImage img = e->viewport()->grab().toImage();
        img.save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/remote_cursors.png"));
        const qreal dpr = img.devicePixelRatio();

        // The caret column: the colour on most of its height.
        const QRect caret = rc->caretRect("ann");
        QVERIFY(!caret.isEmpty());
        int hits = 0;
        for (int y = caret.top(); y <= caret.bottom(); ++y) {
            if (img.pixelColor(int((caret.left() + 0.5) * dpr), int(y * dpr)) == caretColor)
                ++hits;
        }
        QVERIFY2(hits >= caret.height() * 3 / 4, qPrintable(QStringLiteral("%1 of %2").arg(hits).arg(caret.height())));
        // Its name flag is up right after the move, above the caret.
        QVERIFY(rc->flagVisible("ann"));
        const QColor flag = img.pixelColor(int((caret.left() + 2) * dpr), int((caret.top() - 3) * dpr));
        QVERIFY2(caret.top() < 6 || flag == caretColor, qPrintable(flag.name()));

        // The selection: tinted background between the glyphs of "over".
        QTextCursor c(e->document());
        c.setPosition(19);
        const QRect start = e->cursorRect(c);
        c.setPosition(23);
        const QRect end = e->cursorRect(c);
        int tinted = 0, total = 0;
        for (int x = start.right() + 1; x < end.left(); ++x) {
            for (int y = start.top() + 1; y < start.bottom(); ++y) {
                const QColor p = img.pixelColor(int(x * dpr), int(y * dpr));
                ++total;
                if (p.green() > p.red() + 30 && p.green() > p.blue() + 30)
                    ++tinted;
            }
        }
        QVERIFY2(total > 0 && tinted > total / 2, qPrintable(QStringLiteral("%1 of %2").arg(tinted).arg(total)));
        // Outside the selection: no tint.
        const QColor outside = img.pixelColor(int((end.right() + 12) * dpr), int(end.center().y() * dpr));
        QVERIFY(!(outside.green() > outside.red() + 30));
    }

    void flagTimesOutAndHover()
    {
        RemoteCursors* rc = e->remoteCursors();
        rc->setFlagTimeout(50);
        rc->setCursor("ann", "Ann", Qt::red, {9, 0});
        QVERIFY(rc->flagVisible("ann"));
        QTRY_VERIFY_WITH_TIMEOUT(!rc->flagVisible("ann"), 2000);
        rc->hover(rc->caretRect("ann").center());
        QVERIFY(rc->flagVisible("ann"));
        rc->hover(QPoint(-50, -50));
        QVERIFY(!rc->flagVisible("ann"));
        // Through the editor's mouse tracking.
        QTest::mouseMove(e->viewport(), rc->caretRect("ann").center());
        QTRY_VERIFY(rc->flagVisible("ann"));
    }
};

QTEST_MAIN(TestRemoteCursors)
#include "tst_remote_cursors.moc"
