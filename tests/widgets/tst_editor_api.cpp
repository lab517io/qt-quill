// QuillEditor API: content, formatting and selection methods against
// expected Deltas, plus a randomised API run with consistency checks.

#include "EditorTestUtil.h"

#include <QDir>
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

AttributeMap attrs(const char* json)
{
    QByteArray b(json);
    b.replace('\'', '"');
    return QJsonDocument::fromJson(b).object();
}

} // namespace

class TestEditorApi : public QObject
{
    Q_OBJECT

private slots:
    void initialState()
    {
        auto e = testutil::makeEditor(false);
        check(e->getContents(), delta("[{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(e->getLength(), 1);
        QCOMPARE(e->getText(), QStringLiteral("\n"));
    }

    void setContents()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        const Delta doc =
            delta("[{'insert':'Hello','attributes':{'bold':true}},{'insert':'\\n','attributes':{'header':1}}]");
        const Delta change = e->setContents(doc);
        check(e->getContents(), doc, __LINE__);
        check(
            change,
            delta(
                "[{'insert':'Hello','attributes':{'bold':true}},{'insert':'\\n','attributes':{'header':1}},{'delete':1}]"),
            __LINE__);
        QCOMPARE(rec.records.size(), 1);
        QCOMPARE(rec.last().source, Source::Api);
        QVERIFY(!e->document()->isUndoAvailable());
        QCOMPARE(rec.mismatches, 0);
    }

    void insertText()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'abc\\n'}]"));
        check(e->insertText(1, QStringLiteral("X")), delta("[{'retain':1},{'insert':'X'}]"), __LINE__);
        check(e->insertText(0, QStringLiteral("B"), attrs("{'bold':true}")),
              delta("[{'insert':'B','attributes':{'bold':true}}]"), __LINE__);
        check(e->getContents(), delta("[{'insert':'B','attributes':{'bold':true}},{'insert':'aXbc\\n'}]"), __LINE__);
        // Text does not inherit neighbouring formats.
        e->insertText(1, QStringLiteral("p"));
        check(e->getContents(), delta("[{'insert':'B','attributes':{'bold':true}},{'insert':'paXbc\\n'}]"), __LINE__);
        // CR LF is normalised.
        e->insertText(0, QStringLiteral("x\r\ny"));
        QCOMPARE(e->getText(), QStringLiteral("x\nyBpaXbc\n"));
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void insertTextNewlinesKeepLineFormat()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'Title'},{'insert':'\\n','attributes':{'header':1}}]"));
        const Delta change = e->insertText(2, QStringLiteral("a\nb"));
        check(e->getContents(),
              delta("[{'insert':'Tia'},{'insert':'\\n','attributes':{'header':1}},"
                    "{'insert':'btle'},{'insert':'\\n','attributes':{'header':1}}]"),
              __LINE__);
        check(change, delta("[{'retain':2},{'insert':'a'},{'insert':'\\n','attributes':{'header':1}},{'insert':'b'}]"),
              __LINE__);
        // Line formats in the attributes apply to the inserted lines.
        e->setContents(delta("[{'insert':'end\\n'}]"));
        e->insertText(0, QStringLiteral("Head\n"), attrs("{'header':2,'italic':true}"));
        check(e->getContents(),
              delta("[{'insert':'Head','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':2}},"
                    "{'insert':'end\\n'}]"),
              __LINE__);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void insertEmbed()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'ab\\n'}]"));
        const Delta change = e->insertEmbed(1, QStringLiteral("image"), QStringLiteral("x.png"));
        check(change, delta("[{'retain':1},{'insert':{'image':'x.png'}}]"), __LINE__);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':{'image':'x.png'}},{'insert':'b\\n'}]"), __LINE__);
        QCOMPARE(e->getText(), QStringLiteral("a￼b\n"));
        e->insertEmbed(0, QStringLiteral("video"), QStringLiteral("https://v"));
        check(e->getContents(),
              delta("[{'insert':{'video':'https://v'}},{'insert':'a'},{'insert':{'image':'x.png'}},{'insert':'b\\n'}]"),
              __LINE__);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void deleteText()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'abc'},{'insert':'\\n','attributes':{'header':1}},{'insert':'def\\n'}]"));
        check(e->deleteText(1, 1), delta("[{'retain':1},{'delete':1}]"), __LINE__);
        // Across lines: the merged line keeps the format of the last line (Delta semantics).
        e->deleteText(1, 2);
        check(e->getContents(), delta("[{'insert':'adef\\n'}]"), __LINE__);
        // Deleting everything leaves an empty line without formats.
        e->setContents(delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'header':1}}]"));
        e->deleteText(0, 3);
        check(e->getContents(), delta("[{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void formatText()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'ab\\ncd\\n'}]"));
        // Inline formats skip newlines; the returned change says so.
        const Delta change = e->formatText(1, 3, attrs("{'bold':true}"));
        check(e->getContents(),
              delta("[{'insert':'a'},{'insert':'b','attributes':{'bold':true}},{'insert':'\\n'},"
                    "{'insert':'c','attributes':{'bold':true}},{'insert':'d\\n'}]"),
              __LINE__);
        check(
            change,
            delta(
                "[{'retain':1},{'retain':1,'attributes':{'bold':true}},{'retain':1},{'retain':1,'attributes':{'bold':true}}]"),
            __LINE__);
        // false and null remove.
        e->formatText(0, 2, attrs("{'bold':false}"));
        e->formatText(3, 1, QJsonObject{{QStringLiteral("bold"), QJsonValue::Null}});
        check(e->getContents(), delta("[{'insert':'ab\\ncd\\n'}]"), __LINE__);
        // Line formats apply only to newlines in the range.
        e->formatText(0, 3, attrs("{'header':1,'italic':true}"));
        check(
            e->getContents(),
            delta(
                "[{'insert':'ab','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':1}},{'insert':'cd\\n'}]"),
            __LINE__);
        // Unknown attributes keep Delta semantics.
        e->formatText(3, 3, attrs("{'token':'t1'}"));
        check(e->getContents(),
              delta("[{'insert':'ab','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':1}},"
                    "{'insert':'cd','attributes':{'token':'t1'}},{'insert':'\\n'}]"),
              __LINE__);
        e->formatText(3, 3, attrs("{'mark':'m'}"));
        check(
            e->getContents(),
            delta("[{'insert':'ab','attributes':{'italic':true}},{'insert':'\\n','attributes':{'header':1}},"
                  "{'insert':'cd','attributes':{'mark':'m','token':'t1'}},{'insert':'\\n','attributes':{'mark':'m'}}]"),
            __LINE__);
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void formatTextStylesQt()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'abc\\n'}]"));
        e->formatText(0, 2, attrs("{'bold':true,'color':'#FFFF0000','link':'https://q'}"));
        QTextCursor c(e->document());
        c.setPosition(1);
        QVERIFY(c.charFormat().fontWeight() >= QFont::Bold);
        QCOMPARE(c.charFormat().foreground().color(), QColor(Qt::red));
        QVERIFY(c.charFormat().isAnchor());
    }

    void formatLine()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'a\\nb\\nc\\n'}]"));
        const Delta change = e->formatLine(0, 3, attrs("{'list':'ordered'}"));
        check(
            change,
            delta(
                "[{'retain':1},{'retain':1,'attributes':{'list':'ordered'}},{'retain':1},{'retain':1,'attributes':{'list':'ordered'}}]"),
            __LINE__);
        QTextBlock first = e->document()->firstBlock();
        QVERIFY(first.textList());
        QCOMPARE(first.textList(), first.next().textList());
        QVERIFY(!first.next().next().textList());
        // Relative indent.
        e->formatLine(2, 0, attrs("{'indent':'+1'}"));
        e->formatLine(2, 0, attrs("{'indent':'+1'}"));
        e->formatLine(2, 0, attrs("{'indent':'-1'}"));
        check(e->getContents(),
              delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'ordered'}},"
                    "{'insert':'b'},{'insert':'\\n','attributes':{'indent':1,'list':'ordered'}},{'insert':'c\\n'}]"),
              __LINE__);
        QCOMPARE(e->document()->findBlockByNumber(1).textList()->format().style(), QTextListFormat::ListLowerAlpha);
        e->formatLine(2, 0, attrs("{'indent':'-1'}"));
        e->formatLine(2, 0, attrs("{'indent':'-1'}"));
        check(e->getContents().slice(2, 4), delta("[{'insert':'b'},{'insert':'\\n','attributes':{'list':'ordered'}}]"),
              __LINE__);
        // Exclusive block formats; header coexists (flutter_quill).
        e->formatLine(4, 0, attrs("{'blockquote':true}"));
        e->formatLine(4, 0, attrs("{'code-block':true,'header':2}"));
        check(e->getContents().slice(4),
              delta("[{'insert':'c'},{'insert':'\\n','attributes':{'code-block':true,'header':2}}]"), __LINE__);
        e->formatLine(0, 6, attrs("{'list':false,'code-block':null,'header':null}"));
        check(e->getContents(), delta("[{'insert':'a\\nb\\nc\\n'}]"), __LINE__);
        QVERIFY(!e->document()->firstBlock().textList());
        QCOMPARE(e->verificationFailures(), 0);
    }

    void formatLineChecklist()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'todo\\n'}]"));
        e->formatLine(0, 0, attrs("{'list':'unchecked'}"));
        QCOMPARE(e->document()->firstBlock().blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
        e->formatLine(0, 0, attrs("{'list':'checked'}"));
        QCOMPARE(e->document()->firstBlock().blockFormat().marker(), QTextBlockFormat::MarkerType::Checked);
        check(e->getContents(), delta("[{'insert':'todo'},{'insert':'\\n','attributes':{'list':'checked'}}]"),
              __LINE__);
    }

    void removeFormat()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta(
            "[{'insert':'ab','attributes':{'bold':true}},{'insert':'\\n','attributes':{'header':1}},"
            "{'insert':'cd','attributes':{'italic':true}},{'insert':{'image':'x.png'},'attributes':{'width':'10'}},"
            "{'insert':'\\n','attributes':{'list':'bullet'}}]"));
        e->removeFormat(1, 3);
        // Range [1,4) loses formats; the line containing 4 loses its line format
        // but keeps inline formats after the range; embeds keep attributes.
        check(
            e->getContents(),
            delta(
                "[{'insert':'a','attributes':{'bold':true}},{'insert':'b\\nc'},"
                "{'insert':'d','attributes':{'italic':true}},{'insert':{'image':'x.png'},'attributes':{'width':'10'}},"
                "{'insert':'\\n'}]"),
            __LINE__);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void getFormat()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta(
            "[{'insert':'ab','attributes':{'bold':true}},{'insert':'cd','attributes':{'bold':true,'italic':true}},"
            "{'insert':'\\n','attributes':{'header':1}},{'insert':'ef'},{'insert':'\\n','attributes':{'header':2}}]"));
        QCOMPARE(e->getFormat(1), attrs("{'bold':true,'header':1}"));
        QCOMPARE(e->getFormat(3), attrs("{'bold':true,'italic':true,'header':1}"));
        QCOMPARE(e->getFormat(0), attrs("{'bold':true,'header':1}")); // line start: character at
        QCOMPARE(e->getFormat(5), attrs("{'header':2}"));
        QCOMPARE(e->getFormat(1, 2), attrs("{'bold':true,'header':1}"));
        QCOMPARE(e->getFormat(0, 7), attrs("{'header':[1,2]}"));
        e->setSelection(2, 2);
        QCOMPARE(e->getFormat(), attrs("{'bold':true,'italic':true,'header':1}"));
    }

    void formatCollapsedAppliesToTyping()
    {
        auto e = testutil::makeEditor();
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'ab\\n'}]"));
        e->setSelection(1, 0);
        QVERIFY(e->format(QStringLiteral("bold"), true).isEmpty());
        QCOMPARE(e->getFormat(), attrs("{'bold':true}"));
        QTest::keyClicks(e.get(), QStringLiteral("XY"));
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'XY','attributes':{'bold':true}},{'insert':'b\\n'}]"),
              __LINE__);
        QCOMPARE(rec.last().source, Source::User);
        // With a selection, format() formats it and keeps the selection.
        e->setSelection(0, 1);
        e->format(QStringLiteral("italic"), true);
        e->format(QStringLiteral("align"), QStringLiteral("center"));
        check(e->getContents(),
              delta("[{'insert':'a','attributes':{'italic':true}},{'insert':'XY','attributes':{'bold':true}},"
                    "{'insert':'b'},{'insert':'\\n','attributes':{'align':'center'}}]"),
              __LINE__);
        QCOMPARE(e->getSelection(), (Range{0, 1}));
        QCOMPARE(rec.mismatches, 0);
    }

    void updateContents()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'abc'},{'insert':'\\n','attributes':{'header':1}}]"));
        const Delta c1 = delta(
            "[{'retain':1},{'insert':'X','attributes':{'italic':true}},{'retain':1,'attributes':{'bold':true}},{'delete':1}]");
        check(e->updateContents(c1), c1, __LINE__);
        check(
            e->getContents(),
            delta(
                "[{'insert':'a'},{'insert':'X','attributes':{'italic':true}},{'insert':'b','attributes':{'bold':true}},"
                "{'insert':'\\n','attributes':{'header':1}}]"),
            __LINE__);
        // Inserted lines get exactly the inserted newline's attributes.
        e->updateContents(delta("[{'retain':3},{'insert':'\\n','attributes':{'list':'bullet'}}]"));
        check(
            e->getContents(),
            delta(
                "[{'insert':'a'},{'insert':'X','attributes':{'italic':true}},{'insert':'b','attributes':{'bold':true}},"
                "{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':'\\n','attributes':{'header':1}}]"),
            __LINE__);
        // Deleting the final newline leaves a plain one.
        e->updateContents(delta("[{'retain':4},{'delete':1}]"));
        check(
            e->getContents(),
            delta(
                "[{'insert':'a'},{'insert':'X','attributes':{'italic':true}},{'insert':'b','attributes':{'bold':true}},"
                "{'insert':'\\n','attributes':{'list':'bullet'}}]"),
            __LINE__);
        // Inserting after the final newline appends a line.
        e->updateContents(delta("[{'retain':4},{'insert':'tail'}]"));
        check(
            e->getContents(),
            delta(
                "[{'insert':'a'},{'insert':'X','attributes':{'italic':true}},{'insert':'b','attributes':{'bold':true}},"
                "{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':'tail\\n'}]"),
            __LINE__);
        // Retains beyond the end are ignored.
        e->updateContents(delta("[{'retain':100,'attributes':{'underline':true}}]"));
        check(
            e->getContents(),
            delta(
                "[{'insert':'a','attributes':{'underline':true}},{'insert':'X','attributes':{'italic':true,'underline':true}},"
                "{'insert':'b','attributes':{'bold':true,'underline':true}},"
                "{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':'tail','attributes':{'underline':true}},{'insert':'\\n'}]"),
            __LINE__);
        // Replace everything.
        e->updateContents(delta("[{'delete':100},{'insert':'new\\n'}]"));
        check(e->getContents(), delta("[{'insert':'new\\n'}]"), __LINE__);
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void sources()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'ab\\n'}]"), Source::Silent);
        QCOMPARE(rec.records.size(), 0);
        check(e->getContents(), delta("[{'insert':'ab\\n'}]"), __LINE__);
        e->insertText(0, QStringLiteral("x"), {}, Source::User);
        QCOMPARE(rec.last().source, Source::User);
        e->insertText(0, QStringLiteral("x"), {}, Source::Silent);
        QCOMPARE(rec.records.size(), 1);
        QCOMPARE(e->getText(), QStringLiteral("xxab\n"));
        // Read-only: user edits are refused, API edits are not.
        e->setReadOnly(true);
        QVERIFY(e->insertText(0, QStringLiteral("u"), {}, Source::User).isEmpty());
        e->insertText(0, QStringLiteral("a"));
        QCOMPARE(e->getText(), QStringLiteral("axxab\n"));
        // A change that changes nothing emits nothing.
        rec.clear();
        e->formatText(0, 2, attrs("{'bold':null}"));
        QCOMPARE(rec.records.size(), 0);
    }

    void selection()
    {
        auto e = testutil::makeEditor(false);
        e->setContents(delta("[{'insert':'hello\\n'}]"));
        e->setSelection(0, 0, Source::Silent);
        QSignalSpy spy(e.get(), &QuillEditor::selectionChange);
        e->setSelection(1, 3);
        QCOMPARE(e->getSelection(), (Range{1, 3}));
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.last().at(0).value<Range>(), (Range{1, 3}));
        QCOMPARE(spy.last().at(1).value<Range>(), (Range{0, 0}));
        QCOMPARE(spy.last().at(2).value<Source>(), Source::Api);
        e->setSelection(2, 0, Source::Silent);
        QCOMPARE(spy.size(), 1);
        e->setSelection(100, 5); // clamped
        QCOMPARE(e->getSelection(), (Range{5, 0}));
        // Edits by the API move the selection silently.
        spy.clear();
        e->insertText(0, QStringLiteral("ab"));
        QCOMPARE(e->getSelection(), (Range{7, 0}));
        QCOMPARE(spy.size(), 0);
        // User selection changes.
        QTest::keyClick(e.get(), Qt::Key_Left, Qt::ShiftModifier);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.last().at(0).value<Range>(), (Range{6, 1}));
        QCOMPARE(spy.last().at(2).value<Source>(), Source::User);
    }

    void undoRedo()
    {
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'ab\\n'}]"));
        e->formatLine(0, 0, attrs("{'list':'bullet'}"));
        e->insertText(2, QStringLiteral("c"), attrs("{'bold':true}"));
        e->undo();
        check(e->getContents(), delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
        QCOMPARE(rec.last().source, Source::User);
        e->undo();
        check(e->getContents(), delta("[{'insert':'ab\\n'}]"), __LINE__);
        QVERIFY(!e->document()->firstBlock().textList());
        e->redo();
        e->redo();
        check(
            e->getContents(),
            delta(
                "[{'insert':'ab'},{'insert':'c','attributes':{'bold':true}},{'insert':'\\n','attributes':{'list':'bullet'}}]"),
            __LINE__);
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void styleOptionsKeepContents()
    {
        auto e = testutil::makeEditor(false);
        const Delta doc = testutil::delta(
            "[{'insert':'T'},{'insert':'\\n','attributes':{'header':1}},{'insert':'x','attributes':{'size':'large'}},{'insert':'\\n'}]");
        e->setContents(doc);
        StyleOptions s = e->styleOptions();
        s.headerSizes[0] = 40;
        s.baseFont = QFont(QStringLiteral("DejaVu Serif"), 14);
        e->setStyleOptions(s);
        check(e->getContents(), doc, __LINE__);
        check(DeltaDocument::toDelta(e->document()), doc, __LINE__);
        QCOMPARE(QTextCursor(e->document()->firstBlock()).charFormat().fontPointSize(), 14.0 * 40 / 16);
    }

    void fixturesRoundTrip_data()
    {
        QTest::addColumn<QString>("file");
        const QDir dir(testutil::fixturePath(QStringLiteral("documents")));
        for (const QString& f : dir.entryList({QStringLiteral("*.json")}))
            QTest::newRow(qPrintable(f)) << QStringLiteral("documents/") + f;
        QTest::newRow("flutter_quill example") << QStringLiteral("flutter_quill/example_default_sample.json");
    }

    void fixturesRoundTrip()
    {
        QFETCH(QString, file);
        const Delta doc =
            *Delta::fromJson(testutil::loadJson(file).isArray() ? QJsonValue(testutil::loadJson(file).array())
                                                                : QJsonValue(testutil::loadJson(file).object()));
        auto e = testutil::makeEditor(false);
        e->setContents(doc);
        Delta canonical;
        for (const Op& op : doc.ops())
            canonical.push(op);
        check(e->getContents(), canonical, __LINE__);
    }

    // Random API calls on every fixture; every change must compose and the
    // tracked contents must never drift from the document.
    void randomApi_data() { fixturesRoundTrip_data(); }

    void randomApi()
    {
        QFETCH(QString, file);
        const QJsonDocument json = testutil::loadJson(file);
        const Delta doc = *Delta::fromJson(json.isArray() ? QJsonValue(json.array()) : QJsonValue(json.object()));
        auto e = testutil::makeEditor(false);
        ChangeRecorder rec(e.get());
        e->setContents(doc);
        QRandomGenerator rng(quint32(qHash(file)));
        const QList<AttributeMap> inlineChoices{
            attrs("{'bold':true}"),         attrs("{'bold':null}"),    attrs("{'italic':true,'underline':true}"),
            attrs("{'color':'#FF00FF00'}"), attrs("{'size':'large'}"), attrs("{'link':'https://l'}"),
            attrs("{'code':true}"),         attrs("{'script':'sub'}"), attrs("{'background':null,'color':null}"),
            attrs("{'font':'monospace'}"),  attrs("{'small':true}"),   attrs("{'token':'x'}")};
        const QList<AttributeMap> lineChoices{
            attrs("{'header':1}"),        attrs("{'header':null}"),
            attrs("{'list':'bullet'}"),   attrs("{'list':'ordered'}"),
            attrs("{'list':'checked'}"),  attrs("{'list':null}"),
            attrs("{'indent':'+1'}"),     attrs("{'indent':'-1'}"),
            attrs("{'align':'center'}"),  attrs("{'direction':'rtl','align':'right'}"),
            attrs("{'blockquote':true}"), attrs("{'code-block':true}"),
            attrs("{'line-height':1.5}")};
        const QStringList texts{QStringLiteral("x"),    QStringLiteral("hello "),      QStringLiteral("\n"),
                                QStringLiteral("a\nb"), QStringLiteral("é\U0001F600"), QStringLiteral("\n\n")};
        const int steps =
            qEnvironmentVariableIsSet("QUILLQT_FUZZ_STEPS") ? qEnvironmentVariableIntValue("QUILLQT_FUZZ_STEPS") : 150;
        for (int step = 0; step < steps; ++step) {
            const int len = e->getLength();
            const int index = int(rng.bounded(len));
            const int length = int(rng.bounded(std::min(len - index, 12) + 1));
            const Delta before = e->getContents();
            QString op;
            const auto json = [](const AttributeMap& m) {
                return QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact));
            };
            switch (rng.bounded(9)) {
            case 0: {
                const QString t = texts.at(rng.bounded(texts.size()));
                const AttributeMap a =
                    rng.bounded(2) ? inlineChoices.at(rng.bounded(inlineChoices.size())) : AttributeMap();
                op = QStringLiteral("insertText(%1, %2, %3)").arg(index).arg(t, json(a));
                e->insertText(index, t, a);
                break;
            }
            case 1:
                op = QStringLiteral("deleteText(%1, %2)").arg(index).arg(length);
                e->deleteText(index, length);
                break;
            case 2: {
                const AttributeMap a = inlineChoices.at(rng.bounded(inlineChoices.size()));
                op = QStringLiteral("formatText(%1, %2, %3)").arg(index).arg(length).arg(json(a));
                e->formatText(index, length, a);
                break;
            }
            case 3: {
                const AttributeMap a = lineChoices.at(rng.bounded(lineChoices.size()));
                op = QStringLiteral("formatLine(%1, %2, %3)").arg(index).arg(length).arg(json(a));
                e->formatLine(index, length, a);
                break;
            }
            case 4: {
                const QString type = rng.bounded(2) ? QStringLiteral("image") : QStringLiteral("formula");
                op = QStringLiteral("insertEmbed(%1, %2)").arg(index).arg(type);
                e->insertEmbed(index, type, QStringLiteral("e%1").arg(step));
                break;
            }
            case 5:
                op = QStringLiteral("removeFormat(%1, %2)").arg(index).arg(length);
                e->removeFormat(index, length);
                break;
            case 6: {
                Delta change = Delta().retain(index);
                change.remove(rng.bounded(3));
                change.insert(texts.at(rng.bounded(texts.size())), lineChoices.at(rng.bounded(lineChoices.size())));
                change.retain(length, inlineChoices.at(rng.bounded(inlineChoices.size())));
                op = QStringLiteral("updateContents(%1)").arg(QString::fromUtf8(change.toJson()));
                e->updateContents(change);
                break;
            }
            case 7:
                op = QStringLiteral("undo");
                e->undo();
                break;
            default:
                op = QStringLiteral("redo");
                e->redo();
                break;
            }
            if (rec.mismatches || e->verificationFailures() || e->getLength() != e->document()->characterCount()) {
                qWarning("step %d: %s\n  before: %s", step, qPrintable(op), before.toJson().constData());
                break;
            }
        }
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }
};

QTEST_MAIN(TestEditorApi)
#include "tst_editor_api.moc"
