// QuillEditor change tracking for user input: typing, clipboard, drag and
// drop, mouse, edits made with plain Qt calls, and the painted decorations.

#include "EditorTestUtil.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QImage>
#include <QMimeData>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>

using namespace quill;
using testutil::ChangeRecorder;
using testutil::delta;

namespace {

void check(const Delta& actual, const Delta& expected, int line)
{
    if (actual != expected)
        qWarning("line %d\n  actual:   %s\n  expected: %s", line, actual.toJson().constData(),
                 expected.toJson().constData());
    QTest::qCompare(actual, expected, "actual", "expected", __FILE__, line);
}

/// Exposes the protected clipboard hooks.
class TestEditor : public QuillEditor
{
public:
    using QuillEditor::createMimeDataFromSelection;
    using QuillEditor::dropEvent;
    using QuillEditor::insertFromMimeData;
    QString imageSourceFor(const QImage& image) override
    {
        uploaded = image.size();
        return QStringLiteral("https://cdn/img.png");
    }
    QSize uploaded;
};

QPoint viewportPos(QuillEditor* e, int docPos)
{
    QTextCursor c(e->document());
    c.setPosition(docPos);
    return e->cursorRect(c).center();
}

} // namespace

class TestEditorChanges : public QObject
{
    Q_OBJECT

    std::unique_ptr<TestEditor> e;
    std::unique_ptr<ChangeRecorder> rec;

    void start(const char* json, int cursor = 0, int length = 0)
    {
        e->setContents(delta(json));
        e->setSelection(cursor, length);
        rec->clear();
    }

private slots:
    void init()
    {
        e = std::make_unique<TestEditor>();
        e->setVerifyChanges(true);
        e->resize(600, 400);
        e->show();
        QVERIFY(QTest::qWaitForWindowExposed(e.get()));
        rec = std::make_unique<ChangeRecorder>(e.get());
    }

    void cleanup()
    {
        QCOMPARE(rec->mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
        rec.reset();
        e.reset();
    }

    void typingProducesQuillShapedChanges()
    {
        start("[{'insert':'aa\\n'}]", 2);
        QTest::keyClicks(e.get(), QStringLiteral("a"));
        // The cursor decides where an ambiguous insert went, as in Quill.
        check(rec->last().change, delta("[{'retain':2},{'insert':'a'}]"), __LINE__);
        QCOMPARE(rec->last().source, Source::User);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(rec->last().change, delta("[{'retain':2},{'delete':1}]"), __LINE__);
        e->setSelection(0, 2);
        QTest::keyClicks(e.get(), QStringLiteral("b"));
        check(e->getContents(), delta("[{'insert':'b\\n'}]"), __LINE__);
        QCOMPARE(rec->records.size(), 3);
    }

    void typingInHeaderInheritsLineStyleNotAttributes()
    {
        start("[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}}]", 5);
        QTest::keyClicks(e.get(), QStringLiteral("!"));
        check(e->getContents(), delta("[{'insert':'Title!'},{'insert':'\\n','attributes':{'header':1}}]"), __LINE__);
    }

    void undoRedoEmitUserChanges()
    {
        start("[{'insert':'ab\\n'}]", 2);
        QTest::keyClicks(e.get(), QStringLiteral("cd"));
        e->setSelection(0, 4);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        rec->clear();
        QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier);
        check(e->getContents(), delta("[{'insert':'abcd\\n'}]"), __LINE__);
        check(rec->last().change, delta("[{'retain':4,'attributes':{'bold':null}}]"), __LINE__);
        QCOMPARE(rec->last().source, Source::User);
        QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(e->getContents(), delta("[{'insert':'abcd','attributes':{'bold':true}},{'insert':'\\n'}]"), __LINE__);
    }

    void plainQtEditsAreTracked()
    {
        start("[{'insert':'abc\\n'}]");
        QTextCursor c(e->document());
        c.setPosition(1);
        c.setPosition(3, QTextCursor::KeepAnchor);
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        c.mergeCharFormat(bold);
        check(rec->last().change, delta("[{'retain':1},{'retain':2,'attributes':{'bold':true}}]"), __LINE__);
        c.beginEditBlock();
        c.movePosition(QTextCursor::End);
        c.insertBlock();
        c.insertText(QStringLiteral("x"));
        c.endEditBlock();
        QCOMPARE(rec->records.size(), 2);
        // Qt's typing format: "x" continues the bold text before it.
        check(e->getContents(),
              delta("[{'insert':'a'},{'insert':'bc','attributes':{'bold':true}},{'insert':'\\n'},"
                    "{'insert':'x','attributes':{'bold':true}},{'insert':'\\n'}]"),
              __LINE__);
        QCOMPARE(rec->last().source, Source::User);
    }

    void pasteHtml()
    {
        start("[{'insert':'[]\\n'}]", 1);
        QMimeData md;
        md.setHtml(QStringLiteral("<p><b>bold</b> <span style=\"color:#ff0000;font-size:30px\">red</span> "
                                  "<a href=\"https://x.y\">link</a></p><ul><li>item</li></ul>"));
        md.setText(QStringLiteral("ignored"));
        e->insertFromMimeData(&md);
        // Colours and sizes are dropped by default; lists and links survive.
        check(e->getContents(),
              delta("[{'insert':'['},{'insert':'bold','attributes':{'bold':true}},{'insert':' red '},"
                    "{'insert':'link','attributes':{'link':'https://x.y'}},{'insert':'\\nitem'},"
                    "{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':']\\n'}]"),
              __LINE__);
        QCOMPARE(rec->records.size(), 1);
        QCOMPARE(rec->last().source, Source::User);
        QCOMPARE(e->getSelection(), (Range{20, 0}));

        e->setPasteIgnoredAttributes({});
        start("[{'insert':'\\n'}]");
        QMimeData colored;
        colored.setHtml(QStringLiteral("<span style=\"color:#ff0000\">red</span>"));
        e->insertFromMimeData(&colored);
        check(e->getContents(), delta("[{'insert':'red','attributes':{'color':'#FFFF0000'}},{'insert':'\\n'}]"),
              __LINE__);
    }

    void pastePlainText()
    {
        // Plain text takes the format of the character before it, newlines
        // keep the line format (flutter_quill).
        start("[{'insert':'ab','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':2}}]", 2);
        QMimeData md;
        md.setText(QStringLiteral("x\r\ny"));
        e->insertFromMimeData(&md);
        check(e->getContents(),
              delta("[{'insert':'abx','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':2}},"
                    "{'insert':'y','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':2}}]"),
              __LINE__);
        QCOMPARE(e->getSelection(), (Range{5, 0}));
        // In a code block, HTML is pasted as plain text.
        start("[{'insert':'c'},{'insert':'\\n','attributes':{'code-block':true}}]", 1);
        QMimeData html;
        html.setHtml(QStringLiteral("<b>b</b>"));
        html.setText(QStringLiteral("p\nq"));
        e->insertFromMimeData(&html);
        check(
            e->getContents(),
            delta(
                "[{'insert':'cp'},{'insert':'\\n','attributes':{'code-block':true}},{'insert':'q'},{'insert':'\\n','attributes':{'code-block':true}}]"),
            __LINE__);
    }

    void copyAndPasteDelta()
    {
        start(
            "[{'insert':'ab','attributes':{'bold':true}},{'insert':{'formula':'x^2'}},{'insert':'\\n','attributes':{'list':'ordered'}},{'insert':'z\\n'}]",
            1, 3);
        std::unique_ptr<QMimeData> md(e->createMimeDataFromSelection());
        QVERIFY(md->hasFormat(QStringLiteral("application/x-quill-delta+json")));
        check(
            *Delta::fromJson(md->data(QStringLiteral("application/x-quill-delta+json"))),
            delta(
                "[{'insert':'b','attributes':{'bold':true}},{'insert':{'formula':'x^2'}},{'insert':'\\n','attributes':{'list':'ordered'}}]"),
            __LINE__);
        QVERIFY(md->hasHtml());
        e->setSelection(5, 0);
        e->insertFromMimeData(md.get());
        check(
            e->getContents(),
            delta(
                "[{'insert':'ab','attributes':{'bold':true}},{'insert':{'formula':'x^2'}},{'insert':'\\n','attributes':{'list':'ordered'}},"
                "{'insert':'z'},{'insert':'b','attributes':{'bold':true}},{'insert':{'formula':'x^2'}},{'insert':'\\n','attributes':{'list':'ordered'}},{'insert':'\\n'}]"),
            __LINE__);
    }

    void pasteImageAndImageFiles()
    {
        start("[{'insert':'ab\\n'}]", 1);
        QMimeData md;
        md.setImageData(QImage(8, 4, QImage::Format_RGB32));
        e->insertFromMimeData(&md);
        QCOMPARE(e->uploaded, QSize(8, 4));
        check(e->getContents(), delta("[{'insert':'a'},{'insert':{'image':'https://cdn/img.png'}},{'insert':'b\\n'}]"),
              __LINE__);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pic.png"));
        QImage(2, 2, QImage::Format_RGB32).save(path);
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(path)});
        e->setSelection(0, 0);
        e->insertFromMimeData(&files);
        QCOMPARE(e->getContents().ops().first().embed().value(QStringLiteral("image")).toString(),
                 QUrl::fromLocalFile(path).toString());
    }

    void dropText()
    {
        start("[{'insert':'hello world\\n'}]");
        QMimeData md;
        md.setText(QStringLiteral("big "));
        const QPoint at = viewportPos(e.get(), 6);
        QDropEvent drop(at, Qt::CopyAction, &md, Qt::LeftButton, Qt::NoModifier);
        e->dropEvent(&drop);
        check(e->getContents(), delta("[{'insert':'hello big world\\n'}]"), __LINE__);
        QCOMPARE(rec->records.size(), 1);
        QCOMPARE(e->getSelection(), (Range{6, 4}));
    }

    void checkboxClickTogglesThroughApi()
    {
        start("[{'insert':'todo'},{'insert':'\\n','attributes':{'list':'unchecked'}},{'insert':'x\\n'}]", 6);
        QTextBlock block = e->document()->firstBlock();
        const QRectF r = e->document()->documentLayout()->blockBoundingRect(block);
        int markerX = -1;
        for (int x = 0; x < 80 && markerX < 0; ++x)
            if (e->document()->documentLayout()->blockWithMarkerAt(QPointF(x, r.center().y())).isValid())
                markerX = x;
        QVERIFY(markerX >= 0);
        const QPoint p(markerX + 2, int(r.center().y()));
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, p);
        check(e->getContents(),
              delta("[{'insert':'todo'},{'insert':'\\n','attributes':{'list':'checked'}},{'insert':'x\\n'}]"),
              __LINE__);
        QCOMPARE(rec->records.size(), 1);
        QCOMPARE(rec->last().source, Source::User);
        check(rec->last().change, delta("[{'retain':4},{'retain':1,'attributes':{'list':'checked'}}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{6, 0})); // the click does not move the cursor
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, p);
        check(e->getContents(),
              delta("[{'insert':'todo'},{'insert':'\\n','attributes':{'list':'unchecked'}},{'insert':'x\\n'}]"),
              __LINE__);
        // Read-only: no toggle.
        e->setReadOnly(true);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(rec->records.size(), 2);
    }

    void linkActivation()
    {
        start("[{'insert':'go '},{'insert':'here','attributes':{'link':'https://example.org/'}},{'insert':'\\n'}]");
        QSignalSpy spy(e.get(), &QuillEditor::linkActivated);
        const QPoint p = viewportPos(e.get(), 5);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(spy.size(), 0); // editing: plain click places the cursor
        QTest::mouseClick(e->viewport(), Qt::LeftButton, Qt::ControlModifier, p);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.last().at(0).toUrl(), QUrl(QStringLiteral("https://example.org/")));
        e->setReadOnly(true);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, p);
        QCOMPARE(spy.size(), 2);
    }

    void decorationsArePainted()
    {
        start(
            "[{'insert':'quote'},{'insert':'\\n','attributes':{'blockquote':true}},{'insert':'\\n'},"
            "{'insert':'code 1'},{'insert':'\\n','attributes':{'code-block':true}},{'insert':'code 2'},{'insert':'\\n','attributes':{'code-block':true}},"
            "{'insert':'after\\n'}]");
        const StyleOptions style = e->styleOptions();
        QVERIFY(!style.codeBlockBackgroundOnBlocks);
        QCoreApplication::processEvents();
        const QImage img = e->viewport()->grab().toImage();
        img.save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/editor_decorations.png"));
        auto* layout = e->document()->documentLayout();
        // The grab is in device pixels (QT_SCALE_FACTOR=2 in the HiDPI run).
        const qreal dpr = img.devicePixelRatio();
        const auto pixel = [&](qreal x, qreal y) { return img.pixelColor(int(x * dpr), int(y * dpr)); };
        const QRectF quote = layout->blockBoundingRect(e->document()->firstBlock());
        const qreal margin = e->document()->documentMargin();
        QCOMPARE(pixel(margin + 1, quote.center().y()), style.blockquoteBarColor);
        // One panel behind both code lines, including the gap between them.
        const QTextBlock code1 = e->document()->findBlockByNumber(2);
        const QRectF r1 = layout->blockBoundingRect(code1);
        const QRectF r2 = layout->blockBoundingRect(code1.next());
        const qreal x = e->viewport()->width() - margin - 6;
        QCOMPARE(pixel(x, r1.center().y()), style.codeBlockBackground);
        QCOMPARE(pixel(x, r2.top()), style.codeBlockBackground);
        // flutter_quill's vertical content padding is inside the panel, the
        // spacing above the code block is not.
        // (scaled to the editor's font, so read the padding from the lines).
        const qreal padTop = code1.blockFormat().doubleProperty(prop::DecorationTop);
        const qreal padBottom = code1.next().blockFormat().doubleProperty(prop::DecorationBottom);
        QVERIFY(padTop > 4);
        QCOMPARE(padBottom, padTop);
        QCOMPARE(pixel(x, r1.top() - padTop + 2), style.codeBlockBackground);
        QCOMPARE(pixel(x, r1.top() - padTop - 2), e->viewport()->palette().color(QPalette::Base));
        QCOMPARE(pixel(x, r2.bottom() + padBottom - 2), style.codeBlockBackground);
        QCOMPARE(pixel(x, r2.bottom() + padBottom + 2), e->viewport()->palette().color(QPalette::Base));
    }

    // flutter_quill's spacing depends on the neighbouring lines; it must follow
    // every kind of edit, and Qt's own edits must stay one undo step.
    void spacingFollowsEdits()
    {
        start("[{'insert':'para\nb1'},{'insert':'\n','attributes':{'list':'bullet'}},"
              "{'insert':'b2'},{'insert':'\n','attributes':{'list':'bullet'}},{'insert':'after\n'}]");
        QTextDocument* doc = e->document();
        const auto top = [&](int line) { return doc->findBlockByNumber(line).blockFormat().topMargin(); };
        // Space above the list, between its items, none below it.
        QVERIFY(top(1) > 0);
        QVERIFY(top(2) > 0);
        QCOMPARE(top(3), 0.0);
        QCOMPARE(doc->findBlockByNumber(2).blockFormat().bottomMargin(), 0.0);

        // Qt's own word deletion at the start of "b2" joins it to "b1".
        e->setSelection(8, 0);
        QTest::keyClick(e.get(), Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(e->getText(), QStringLiteral("para\nb1b2\nafter\n"));
        QCOMPARE(e->verificationFailures(), 0);
        QCOMPARE(top(2), 0.0);                                    // "after" follows the list directly
        QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier); // one step undoes it
        QCOMPARE(e->getText(), QStringLiteral("para\nb1\nb2\nafter\n"));
        QVERIFY(top(2) > 0);
        QCOMPARE(top(3), 0.0);
        QCOMPARE(e->verificationFailures(), 0);

        // API edits: a code-block line gets its padding, inside the panel.
        e->formatLine(e->getLength() - 2, 1, {{QStringLiteral("code-block"), true}});
        const QTextBlockFormat code = doc->lastBlock().blockFormat();
        QVERIFY(code.doubleProperty(prop::DecorationTop) > 0);
        QVERIFY(code.topMargin() > code.doubleProperty(prop::DecorationTop));       // plus the space above the block
        QCOMPARE(code.bottomMargin(), code.doubleProperty(prop::DecorationBottom)); // last line: padding below
        QCOMPARE(e->verificationFailures(), 0);
    }

    void renderFlutterQuillSample()
    {
        const QJsonDocument json = testutil::loadJson(QStringLiteral("flutter_quill/example_default_sample.json"));
        e->setContents(*Delta::fromJson(QJsonValue(json.array())));
        e->resize(700, 1400);
        QCoreApplication::processEvents();
        const QImage img = e->viewport()->grab().toImage();
        QVERIFY(img.save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/editor_flutter_quill_sample.png")));
    }

    void styleChangeKeepsUndoableContents()
    {
        start("[{'insert':'x','attributes':{'size':'huge'}},{'insert':'\\n'}]");
        QFont f = e->font();
        f.setPointSize(f.pointSize() + 4);
        e->setFont(f); // base font follows the widget font
        check(e->getContents(), delta("[{'insert':'x','attributes':{'size':'huge'}},{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(rec->records.size(), 0);
        QTextCursor c(e->document());
        c.setPosition(1);
        QCOMPARE(c.charFormat().fontPointSize(), f.pointSizeF() * 22 / 16);
    }
};

QTEST_MAIN(TestEditorChanges)
#include "tst_editor_changes.moc"
