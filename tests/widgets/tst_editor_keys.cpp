// QuillEditor keyboard behaviour: each row of the M5 keyboard table, and a
// fuzz run of random key events with the drift check enabled.

#include "EditorTestUtil.h"

#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTextBlock>
#include <QTextList>

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

} // namespace

class TestEditorKeys : public QObject
{
    Q_OBJECT

    std::unique_ptr<QuillEditor> e;
    std::unique_ptr<ChangeRecorder> rec;

    void start(const char* json, int cursor, int length = 0)
    {
        e->setContents(delta(json));
        e->setSelection(cursor, length);
        rec->clear();
    }

private slots:
    void init()
    {
        e = testutil::makeEditor();
        rec = std::make_unique<ChangeRecorder>(e.get());
    }

    void cleanup()
    {
        QCOMPARE(rec->mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
        rec.reset();
        e.reset();
    }

    void enterInListContinuesList()
    {
        start("[{'insert':'item'},{'insert':'\\n','attributes':{'list':'bullet','indent':1}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(e->getContents(),
              delta("[{'insert':'it'},{'insert':'\\n','attributes':{'indent':1,'list':'bullet'}},"
                    "{'insert':'em'},{'insert':'\\n','attributes':{'indent':1,'list':'bullet'}}]"),
              __LINE__);
        QCOMPARE(e->getSelection(), (Range{3, 0}));
        QCOMPARE(rec->records.size(), 1);
        QCOMPARE(rec->last().source, Source::User);
        check(rec->last().change, delta("[{'retain':2},{'insert':'\\n','attributes':{'indent':1,'list':'bullet'}}]"),
              __LINE__);
        QCOMPARE(e->document()->firstBlock().textList(), e->document()->firstBlock().next().textList());
    }

    void enterOnEmptyListItemLeavesList()
    {
        start("[{'insert':'a'},{'insert':'\\n\\n','attributes':{'list':'ordered','indent':2}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(e->getContents(),
              delta("[{'insert':'a'},{'insert':'\\n','attributes':{'indent':2,'list':'ordered'}},{'insert':'\\n'}]"),
              __LINE__);
        QCOMPARE(e->getSelection(), (Range{2, 0}));
        QVERIFY(!e->document()->lastBlock().textList());
    }

    void enterCheckedItemMakesUnchecked()
    {
        e->setEditingRules(QuillEditor::EditingRules::QuillJs);
        start("[{'insert':'done'},{'insert':'\\n','attributes':{'list':'checked'}}]", 4);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(
            e->getContents(),
            delta(
                "[{'insert':'done'},{'insert':'\\n','attributes':{'list':'checked'}},{'insert':'\\n','attributes':{'list':'unchecked'}}]"),
            __LINE__);
        QCOMPARE(e->document()->lastBlock().blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
    }

    void enterAtHeaderEndMakesParagraph()
    {
        start("[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}}]", 5);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClicks(e.get(), QStringLiteral("body"));
        check(e->getContents(),
              delta("[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}},{'insert':'body\\n'}]"), __LINE__);
        // In the middle of a header both halves stay headers (Quill).
        e->setSelection(2, 0);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(
            e->getContents(),
            delta(
                "[{'insert':'Ti'},{'insert':'\\n','attributes':{'header':1}},{'insert':'tle'},{'insert':'\\n','attributes':{'header':1}},{'insert':'body\\n'}]"),
            __LINE__);
    }

    void enterCarriesInlineFormats()
    {
        start("[{'insert':'ab','attributes':{'bold':true,'link':'https://x'}},{'insert':'\\n'}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClicks(e.get(), QStringLiteral("c"));
        check(e->getContents(),
              delta("[{'insert':'ab','attributes':{'bold':true,'link':'https://x'}},{'insert':'\\n'},"
                    "{'insert':'c','attributes':{'bold':true}},{'insert':'\\n'}]"),
              __LINE__);
    }

    void enterInBlockquoteAndCodeBlock()
    {
        start("[{'insert':'q'},{'insert':'\\n','attributes':{'blockquote':true}}]", 1);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(e->getContents(), delta("[{'insert':'q'},{'insert':'\\n\\n','attributes':{'blockquote':true}}]"),
              __LINE__);
        QTest::keyClick(e.get(), Qt::Key_Return); // empty last line leaves the quote
        check(e->getContents(),
              delta("[{'insert':'q'},{'insert':'\\n','attributes':{'blockquote':true}},{'insert':'\\n'}]"), __LINE__);

        start(
            "[{'insert':'x'},{'insert':'\\n','attributes':{'code-block':true}},{'insert':'y'},{'insert':'\\n','attributes':{'code-block':true}}]",
            1);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Return); // empty line inside the block: stays code
        check(e->getContents(),
              delta("[{'insert':'x'},{'insert':'\\n\\n\\n','attributes':{'code-block':true}},"
                    "{'insert':'y'},{'insert':'\\n','attributes':{'code-block':true}}]"),
              __LINE__);
        e->setSelection(6, 0);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Return); // empty last code line: exit
        check(e->getContents(),
              delta("[{'insert':'x'},{'insert':'\\n\\n\\n','attributes':{'code-block':true}},"
                    "{'insert':'y'},{'insert':'\\n','attributes':{'code-block':true}},{'insert':'\\n'}]"),
              __LINE__);
    }

    void shiftEnterSplitsKeepingFormat()
    {
        start("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':'\\n'}]", 0);
        e->setSelection(3, 0);
        // Shift+Enter on an empty line has no special rules, and never makes a soft break.
        start("[{'insert':'\\n','attributes':{'list':'bullet'}}]", 0);
        QTest::keyClick(e.get(), Qt::Key_Return, Qt::ShiftModifier);
        check(e->getContents(), delta("[{'insert':'\\n\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        QVERIFY(!e->toPlainText().contains(QChar::LineSeparator));
    }

    void enterReplacesSelection()
    {
        start("[{'insert':'abc'},{'insert':'\\n','attributes':{'header':2}},{'insert':'def\\n'}]", 1, 4);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'header':2}},{'insert':'ef\\n'}]"),
              __LINE__);
    }

    void backspaceOutdentsThenRemovesList()
    {
        e->setEditingRules(QuillEditor::EditingRules::QuillJs);
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet','indent':1}}]", 0);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'a\\n'}]"), __LINE__);
        QVERIFY(!e->document()->firstBlock().textList());
        QTest::keyClick(e.get(), Qt::Key_Backspace); // start of document: nothing
        check(e->getContents(), delta("[{'insert':'a\\n'}]"), __LINE__);
    }

    void backspaceRemovesBlockFormatsFirst()
    {
        e->setEditingRules(QuillEditor::EditingRules::QuillJs);
        start("[{'insert':'p\\nh'},{'insert':'\\n','attributes':{'header':1}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'p\\nh\\n'}]"), __LINE__);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'ph\\n'}]"), __LINE__);

        // Inside a code block, Backspace merges with the previous code line.
        start(
            "[{'insert':'a'},{'insert':'\\n','attributes':{'code-block':true}},{'insert':'b'},{'insert':'\\n','attributes':{'code-block':true}}]",
            2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'code-block':true}}]"), __LINE__);
        // At the first line of a quote it removes the quote.
        start("[{'insert':'p\\nq'},{'insert':'\\n','attributes':{'blockquote':true}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'p\\nq\\n'}]"), __LINE__);
    }

    void backspaceMergeKeepsPreviousFormat()
    {
        // Previous line not empty: the merged line keeps its format (Quill).
        start(
            "[{'insert':'a'},{'insert':'\\n','attributes':{'align':'center'}},{'insert':'b'},{'insert':'\\n','attributes':{'align':'right'}}]",
            2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'align':'center'}}]"), __LINE__);
        check(rec->last().change,
              delta("[{'retain':1},{'delete':1},{'retain':1},{'retain':1,'attributes':{'align':'center'}}]"), __LINE__);
        // Previous line empty: the current line's format survives.
        start("[{'insert':'\\nb'},{'insert':'\\n','attributes':{'align':'right'}}]", 1);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'b'},{'insert':'\\n','attributes':{'align':'right'}}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{0, 0}));
    }

    void backspaceAndDeleteInsideText()
    {
        start("[{'insert':'abc','attributes':{'bold':true}},{'insert':'\\ndef\\n'}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(rec->last().change, delta("[{'retain':1},{'delete':1}]"), __LINE__);
        QTest::keyClick(e.get(), Qt::Key_Delete);
        QTest::keyClick(e.get(), Qt::Key_Delete); // joins the lines, first line's format
        check(e->getContents(), delta("[{'insert':'a','attributes':{'bold':true}},{'insert':'def\\n'}]"), __LINE__);
    }

    void flutterRules()
    {
        QCOMPARE(e->editingRules(), QuillEditor::EditingRules::FlutterQuill);
        // Enter on a checked item continues checked.
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'checked'}}]", 1);
        QTest::keyClick(e.get(), Qt::Key_Return);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n\\n','attributes':{'list':'checked'}}]"),
              __LINE__);
        // Backspace at a list item start merges, keeping the upper line's format.
        start(
            "[{'insert':'a'},{'insert':'\\n','attributes':{'header':1}},{'insert':'b'},{'insert':'\\n','attributes':{'list':'bullet','indent':1}}]",
            2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'header':1}}]"), __LINE__);
        check(
            rec->last().change,
            delta(
                "[{'retain':1},{'delete':1},{'retain':1},{'retain':1,'attributes':{'header':1,'indent':null,'list':null}}]"),
            __LINE__);
        QCOMPARE(e->getSelection(), (Range{1, 0}));
        // Backspace in an empty list item removes the line.
        start("[{'insert':'a'},{'insert':'\\n\\n','attributes':{'list':'bullet'}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        // Backspace at the document start does nothing.
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}}]", 0);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        // Deleting the last "\n" is prevented.
        start("[{'insert':'ab\\n'}]", 1, 2);
        QTest::keyClick(e.get(), Qt::Key_Delete);
        check(e->getContents(), delta("[{'insert':'a\\n'}]"), __LINE__);
    }

    void selectionDeleteKeepsLastLineFormat()
    {
        // A selection across lines is deleted as a Delta: the remaining line
        // keeps the format of the last line (Qt would keep the first one's).
        for (int key : {int(Qt::Key_Backspace), int(Qt::Key_Delete), int(Qt::Key_X)}) {
            start(
                "[{'insert':'one'},{'insert':'\\n','attributes':{'header':1}},{'insert':'two'},{'insert':'\\n','attributes':{'list':'bullet'}}]",
                2, 3);
            if (key == Qt::Key_X)
                QTest::keyClicks(e.get(), QStringLiteral("X"));
            else
                QTest::keyClick(e.get(), Qt::Key(key));
            const QString text = key == Qt::Key_X ? QStringLiteral("onXwo") : QStringLiteral("onwo");
            check(e->getContents(),
                  delta(QStringLiteral("[{'insert':'%1'},{'insert':'\\n','attributes':{'list':'bullet'}}]")
                            .arg(text)
                            .toUtf8()
                            .constData()),
                  __LINE__);
            QVERIFY(e->document()->firstBlock().textList());
        }
        // Ctrl+X: copied, then deleted the same way.
        start(
            "[{'insert':'one'},{'insert':'\\n','attributes':{'header':1}},{'insert':'two'},{'insert':'\\n','attributes':{'list':'bullet'}}]",
            2, 3);
        QTest::keyClick(e.get(), Qt::Key_X, Qt::ControlModifier);
        check(e->getContents(), delta("[{'insert':'onwo'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
    }

    void typingFormatFollowsLineRestyle()
    {
        // Leaving a quote on an empty line: typed text is plain.
        start("[{'insert':'q'},{'insert':'\\n\\n','attributes':{'blockquote':true}}]", 2);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClicks(e.get(), QStringLiteral("out"));
        check(e->getContents(),
              delta("[{'insert':'q'},{'insert':'\\n','attributes':{'blockquote':true}},{'insert':'out\\n'}]"),
              __LINE__);
        // A code block toggled on an empty line: typed text has no font.
        start("[{'insert':'a\\n\\n'}]", 2);
        e->format(QStringLiteral("code-block"), true, Source::User);
        QTest::keyClicks(e.get(), QStringLiteral("x"));
        e->format(QStringLiteral("code-block"), QJsonValue::Null, Source::User);
        QTest::keyClicks(e.get(), QStringLiteral("y"));
        check(e->getContents(), delta("[{'insert':'a\\nxy\\n'}]"), __LINE__);
    }

    void linkStopsAfterApiSelection()
    {
        start("[{'insert':'x\\n'}]", 0, 1);
        e->format(QStringLiteral("link"), QStringLiteral("https://a.b"), Source::User);
        e->setSelection(1, 0);
        QTest::keyClicks(e.get(), QStringLiteral("y"));
        check(e->getContents(), delta("[{'insert':'x','attributes':{'link':'https://a.b'}},{'insert':'y\\n'}]"),
              __LINE__);
    }

    void tabIndentsLists()
    {
        // At the start of a list item (elsewhere Tab inserts "\t").
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'ordered'}}]", 0);
        QTest::keyClick(e.get(), Qt::Key_Tab);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'indent':1,'list':'ordered'}}]"),
              __LINE__);
        QCOMPARE(e->document()->firstBlock().textList()->format().style(), QTextListFormat::ListLowerAlpha);
        QTest::keyClick(e.get(), Qt::Key_Tab, Qt::ShiftModifier);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'ordered'}}]"), __LINE__);
        for (int i = 0; i < 10; ++i)
            QTest::keyClick(e.get(), Qt::Key_Tab);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'indent':8,'list':'ordered'}}]"),
              __LINE__);
    }

    void tabElsewhere()
    {
        start("[{'insert':'ab','attributes':{'italic':true}},{'insert':'\\n'}]", 1);
        QTest::keyClick(e.get(), Qt::Key_Tab);
        check(e->getContents(), delta("[{'insert':'a\\tb','attributes':{'italic':true}},{'insert':'\\n'}]"), __LINE__);
        // Several lines selected outside lists: flutter_quill ignores Tab,
        // Quill.js indents them.
        start("[{'insert':'a\\nb\\n'}]", 0, 3);
        QTest::keyClick(e.get(), Qt::Key_Tab);
        check(e->getContents(), delta("[{'insert':'a\\nb\\n'}]"), __LINE__);
        e->setEditingRules(QuillEditor::EditingRules::QuillJs);
        QTest::keyClick(e.get(), Qt::Key_Tab);
        check(
            e->getContents(),
            delta(
                "[{'insert':'a'},{'insert':'\\n','attributes':{'indent':1}},{'insert':'b'},{'insert':'\\n','attributes':{'indent':1}}]"),
            __LINE__);
        QTest::keyClick(e.get(), Qt::Key_Backtab);
        check(e->getContents(), delta("[{'insert':'a\\nb\\n'}]"), __LINE__);
    }

    void inlineShortcuts()
    {
        start("[{'insert':'abc\\n'}]", 0, 2);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClick(e.get(), Qt::Key_I, Qt::ControlModifier);
        QTest::keyClick(e.get(), Qt::Key_U, Qt::ControlModifier);
        QTest::keyClick(e.get(), Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier);
        check(
            e->getContents(),
            delta(
                "[{'insert':'ab','attributes':{'bold':true,'italic':true,'strike':true,'underline':true}},{'insert':'c\\n'}]"),
            __LINE__);
        QCOMPARE(rec->last().source, Source::User);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier); // toggles off
        check(e->getContents(),
              delta("[{'insert':'ab','attributes':{'italic':true,'strike':true,'underline':true}},{'insert':'c\\n'}]"),
              __LINE__);
        // Collapsed: applies to typing.
        e->setSelection(3, 0);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClicks(e.get(), QStringLiteral("d"));
        check(e->getContents(),
              delta("[{'insert':'ab','attributes':{'italic':true,'strike':true,'underline':true}},{'insert':'c'},"
                    "{'insert':'d','attributes':{'bold':true}},{'insert':'\\n'}]"),
              __LINE__);
    }

    void ctrlKRequestsLink()
    {
        QSignalSpy spy(e.get(), &QuillEditor::linkRequested);
        QTest::keyClick(e.get(), Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(spy.size(), 1);
    }

    void listAutofill_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("list");
        QTest::newRow("1.") << QStringLiteral("1.") << QStringLiteral("ordered");
        QTest::newRow("12.") << QStringLiteral("12.") << QStringLiteral("ordered");
        QTest::newRow("-") << QStringLiteral("-") << QStringLiteral("bullet");
        QTest::newRow("*") << QStringLiteral("*") << QStringLiteral("bullet");
        QTest::newRow("[]") << QStringLiteral("[]") << QStringLiteral("unchecked");
        QTest::newRow("[ ]") << QStringLiteral("[ ]") << QStringLiteral("unchecked");
        QTest::newRow("[x]") << QStringLiteral("[x]") << QStringLiteral("checked");
    }

    void listAutofill()
    {
        QFETCH(QString, typed);
        QFETCH(QString, list);
        start("[{'insert':'rest\\n'}]", 0);
        QTest::keyClicks(e.get(), typed);
        QTest::keyClick(e.get(), Qt::Key_Space);
        Delta expected;
        expected.insert(QStringLiteral("rest"))
            .insert(QStringLiteral("\n"), QJsonObject{{QStringLiteral("list"), list}});
        check(e->getContents(), expected, __LINE__);
        QCOMPARE(e->getSelection(), (Range{0, 0}));
        // Undo brings back the typed marker and space.
        e->undo();
        QCOMPARE(e->getText(), typed + QStringLiteral(" rest\n"));
    }

    void noAutofillInCodeOrMidLine()
    {
        start("[{'insert':'x'},{'insert':'\\n','attributes':{'code-block':true}}]", 0);
        QTest::keyClicks(e.get(), QStringLiteral("- "));
        QCOMPARE(e->getText(), QStringLiteral("- x\n"));
        start("[{'insert':'ab\\n'}]", 2);
        QTest::keyClicks(e.get(), QStringLiteral("- "));
        QCOMPARE(e->getText(), QStringLiteral("ab- \n"));
    }

    void typingAfterImageIsPlainText()
    {
        start("[{'insert':'a'},{'insert':{'image':'x.png'},'attributes':{'width':'30','bold':true}},{'insert':'\\n'}]",
              2);
        QTest::keyClicks(e.get(), QStringLiteral("t"));
        check(e->getContents(),
              delta("[{'insert':'a'},{'insert':{'image':'x.png'},'attributes':{'bold':true,'width':'30'}},"
                    "{'insert':'t','attributes':{'bold':true}},{'insert':'\\n'}]"),
              __LINE__);
    }

    void typingAtLinkEndDoesNotExtendIt()
    {
        start("[{'insert':'see '},{'insert':'link','attributes':{'link':'https://l'}},{'insert':'\\n'}]", 8);
        QTest::keyClicks(e.get(), QStringLiteral("!"));
        e->setSelection(6, 0);
        QTest::keyClicks(e.get(), QStringLiteral("I"));
        check(e->getContents(),
              delta("[{'insert':'see '},{'insert':'liInk','attributes':{'link':'https://l'}},{'insert':'!\\n'}]"),
              __LINE__);
    }

    void readOnlyIgnoresEditingKeys()
    {
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'bullet'}}]", 1);
        e->setReadOnly(true);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClicks(e.get(), QStringLiteral("xyz"));
        QCOMPARE(rec->records.size(), 0);
    }

    // Random key events; every change must compose and contents must never drift.
    void fuzzKeys()
    {
        const QStringList docs{
            QStringLiteral("documents/lists_nested.json"), QStringLiteral("documents/blockquote_code.json"),
            QStringLiteral("documents/headers.json"), QStringLiteral("flutter_quill/example_default_sample.json")};
        const int events =
            qEnvironmentVariableIsSet("QUILLQT_FUZZ_STEPS") ? qEnvironmentVariableIntValue("QUILLQT_FUZZ_STEPS") : 2500;
        QRandomGenerator rng(7);
        struct Key
        {
            int key;
            Qt::KeyboardModifiers mods;
            QString text;
        };
        const QList<Key> keys{
            {Qt::Key_Return, {}, {}},
            {Qt::Key_Return, Qt::ShiftModifier, {}},
            {Qt::Key_Backspace, {}, {}},
            {Qt::Key_Delete, {}, {}},
            {Qt::Key_Tab, {}, {}},
            {Qt::Key_Backtab, Qt::ShiftModifier, {}},
            {Qt::Key_Left, {}, {}},
            {Qt::Key_Right, {}, {}},
            {Qt::Key_Up, {}, {}},
            {Qt::Key_Down, {}, {}},
            {Qt::Key_Home, {}, {}},
            {Qt::Key_End, Qt::ShiftModifier, {}},
            {Qt::Key_Left, Qt::ShiftModifier, {}},
            {Qt::Key_Right, Qt::ShiftModifier, {}},
            {Qt::Key_B, Qt::ControlModifier, {}},
            {Qt::Key_I, Qt::ControlModifier, {}},
            {Qt::Key_Z, Qt::ControlModifier, {}},
            {Qt::Key_Y, Qt::ControlModifier, {}},
            {Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier, {}},
            {Qt::Key_Space, {}, QStringLiteral(" ")},
            {Qt::Key_Minus, {}, QStringLiteral("-")},
            {Qt::Key_1, {}, QStringLiteral("1")},
            {Qt::Key_Period, {}, QStringLiteral(".")},
            {Qt::Key_A, {}, QStringLiteral("a")},
            {Qt::Key_X, {}, QStringLiteral("x")},
            {Qt::Key_X, Qt::ControlModifier, {}},
            {Qt::Key_V, Qt::ControlModifier, {}},
            {Qt::Key_C, Qt::ControlModifier, {}},
            {Qt::Key_A, Qt::ControlModifier, {}},
            {Qt::Key_Backspace, Qt::ControlModifier, {}},
        };
        for (const QString& file : docs) {
            const QJsonDocument json = testutil::loadJson(file);
            e->setContents(*Delta::fromJson(json.isArray() ? QJsonValue(json.array()) : QJsonValue(json.object())));
            for (int i = 0; i < events; ++i) {
                if (i % 200 == 0)
                    e->setSelection(int(rng.bounded(e->getLength())), 0);
                const Key& k = keys.at(rng.bounded(keys.size()));
                QTest::keyClick(e.get(), Qt::Key(k.key), k.mods);
                if (k.key != Qt::Key_Space && !k.text.isEmpty() && rng.bounded(3) == 0)
                    QTest::keyClicks(e.get(), k.text);
                if (rec->mismatches || e->verificationFailures() || e->getLength() != e->document()->characterCount()) {
                    qWarning("%s: event %d (key %x mods %x)", qPrintable(file), i, k.key, int(k.mods));
                    return;
                }
            }
        }
    }
};

QTEST_MAIN(TestEditorKeys)
#include "tst_editor_keys.moc"
