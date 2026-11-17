// The Delta undo model (quill::History): Quill.js's history tests
// (test/unit/modules/history.spec.ts) ported to QuillEditor, flutter_quill's
// selection rule, and every undo path with a remote change in between.
#include "EditorTestUtil.h"
#include "QuillEditor_p.h"

#include <quill/History.h>
#include <quill/QuillToolbar.h>

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

using quill::AttributeMap;
using quill::Delta;
using quill::History;
using quill::HistoryOptions;
using quill::QuillEditor;
using quill::Source;
using testutil::delta;

namespace {

std::unique_ptr<QuillEditor> setup(HistoryOptions options, bool show = false)
{
    auto e = testutil::makeEditor(show);
    e->setContents(Delta().insert("The lazy fox\n"));
    e->installHistory(options);
    return e;
}

HistoryOptions opts(int delay, bool userOnly = false, int maxStack = 100)
{
    HistoryOptions o;
    o.delay = delay;
    o.userOnly = userOnly;
    o.maxStack = maxStack;
    return o;
}

void check(const Delta& actual, const Delta& expected, int line)
{
    if (actual != expected)
        qWarning("line %d", line);
    QCOMPARE(actual.toJson(), expected.toJson());
}

} // namespace

class TestHistory : public QObject
{
    Q_OBJECT

private slots:
    // --- getLastChangeIndex ------------------------------------------------------

    void lastChangeIndex_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<int>("index");
        QTest::newRow("delete") << "[{'retain':4},{'delete':2}]" << 4;
        QTest::newRow("delete with inserts") << "[{'retain':4},{'insert':'test'},{'delete':2}]" << 8;
        QTest::newRow("insert text") << "[{'retain':4},{'insert':'testing'}]" << 11;
        QTest::newRow("insert embed") << "[{'retain':4},{'insert':{'image':true}}]" << 5;
        QTest::newRow("insert with deletes") << "[{'retain':4},{'delete':3},{'insert':'!'}]" << 5;
        QTest::newRow("format") << "[{'retain':4},{'retain':3,'attributes':{'bold':true}}]" << 7;
        QTest::newRow("format newline") << "[{'retain':4},{'retain':1,'attributes':{'align':'left'}}]" << 4;
        QTest::newRow("format mixed") << "[{'retain':4},{'retain':1,'attributes':{'align':'left','bold':true}}]" << 4;
        QTest::newRow("insert newline") << "[{'retain':4},{'insert':'a\\n'}]" << 5;
        QTest::newRow("multiple newline inserts") << "[{'retain':4},{'insert':'ab\\n\\n'}]" << 7;
    }

    void lastChangeIndex()
    {
        QFETCH(QString, change);
        QFETCH(int, index);
        QCOMPARE(History::lastChangeIndex(delta(change.toUtf8().constData())), index);
    }

    // --- undo/redo (Quill.js) ------------------------------------------------------

    void limitsUndoStackSize()
    {
        auto e = setup(opts(0, false, 2));
        for (const char* t : {"A", "B", "C"})
            e->insertText(0, QString::fromLatin1(t));
        QCOMPARE(e->history()->undoStack().size(), 2);
    }

    void mergeChanges()
    {
        auto e = setup(opts(400));
        const Delta original = e->getContents();
        QCOMPARE(e->history()->undoStack().size(), 0);
        e->updateContents(delta("[{'retain':12},{'insert':'e'}]"));
        QCOMPARE(e->history()->undoStack().size(), 1);
        e->updateContents(delta("[{'retain':13},{'insert':'s'}]"));
        QCOMPARE(e->history()->undoStack().size(), 1);
        e->history()->undo();
        check(e->getContents(), original, __LINE__);
        QCOMPARE(e->history()->undoStack().size(), 0);
    }

    void dontMergeChanges()
    {
        auto e = setup(opts(100));
        e->updateContents(delta("[{'retain':12},{'insert':'e'}]"));
        QCOMPARE(e->history()->undoStack().size(), 1);
        QTest::qWait(125);
        e->updateContents(delta("[{'retain':13},{'insert':'s'}]"));
        QCOMPARE(e->history()->undoStack().size(), 2);
    }

    void multipleUndos()
    {
        auto e = setup(opts(100));
        const Delta original = e->getContents();
        e->updateContents(delta("[{'retain':12},{'insert':'e'}]"));
        const Delta contents = e->getContents();
        QTest::qWait(125);
        e->updateContents(delta("[{'retain':13},{'insert':'s'}]"));
        e->history()->undo();
        check(e->getContents(), contents, __LINE__);
        e->history()->undo();
        check(e->getContents(), original, __LINE__);
    }

    void transformApiChange()
    {
        auto e = setup(opts(400, true));
        e->updateContents(delta("[{'retain':12},{'insert':'es'}]"), Source::User);
        e->history()->cutoff();
        e->updateContents(delta("[{'retain':14},{'insert':'!'}]"), Source::User);
        e->history()->undo();
        e->updateContents(delta("[{'retain':4},{'delete':5}]"), Source::Api);
        check(e->getContents(), Delta().insert("The foxes\n"), __LINE__);
        e->history()->undo();
        check(e->getContents(), Delta().insert("The fox\n"), __LINE__);
        e->history()->redo();
        check(e->getContents(), Delta().insert("The foxes\n"), __LINE__);
        e->history()->redo();
        check(e->getContents(), Delta().insert("The foxes!\n"), __LINE__);
    }

    void transformPreserveIntention()
    {
        auto e = setup(opts(400, true));
        const QString url = QStringLiteral("https://www.google.com/");
        const AttributeMap link{{"link", url}};
        e->updateContents(Delta().insert(url, link), Source::User);
        e->history()->cutoff();
        e->updateContents(Delta().remove(url.size()).insert("Google", link), Source::Api);
        e->history()->cutoff();
        e->updateContents(Delta().retain(e->getLength() - 1).insert("!"), Source::User);
        e->history()->cutoff();
        check(e->getContents(), Delta().insert("Google", link).insert("The lazy fox!\n"), __LINE__);
        e->history()->undo();
        check(e->getContents(), Delta().insert("Google", link).insert("The lazy fox\n"), __LINE__);
        e->history()->undo();
        check(e->getContents(), Delta().insert("Google", link).insert("The lazy fox\n"), __LINE__);
    }

    void ignoreRemoteChanges()
    {
        auto e = setup(opts(0, true));
        e->setContents(Delta().insert("\n"));
        e->insertText(0, "a", {}, Source::User);
        e->insertText(1, "b", {}, Source::Api);
        e->insertText(2, "c", {}, Source::User);
        e->insertText(3, "d", {}, Source::Api);
        QCOMPARE(e->getText(), QString("abcd\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("abd\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("bd\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("abd\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("abcd\n"));
    }

    void transformAgainstRemoteChanges()
    {
        auto e = setup(opts(0, true));
        e->setContents(Delta().insert("b\n"));
        e->insertText(1, "d", {}, Source::User);
        e->insertText(0, "a", {}, Source::User);
        e->insertText(2, "c", {}, Source::Api);
        QCOMPARE(e->getText(), QString("abcd\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("bcd\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("bc\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("bcd\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("abcd\n"));
    }

    void transformAgainstRemoteChangesBreakingUpAnInsert()
    {
        auto e = setup(opts(0, true));
        e->setContents(Delta().insert("\n"));
        e->insertText(0, "ABC", {}, Source::User);
        e->insertText(3, "4", {}, Source::Api);
        e->insertText(2, "3", {}, Source::Api);
        e->insertText(1, "2", {}, Source::Api);
        e->insertText(0, "1", {}, Source::Api);
        QCOMPARE(e->getText(), QString("1A2B3C4\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("1234\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("1A2B3C4\n"));
        e->history()->undo();
        QCOMPARE(e->getText(), QString("1234\n"));
        e->history()->redo();
        QCOMPARE(e->getText(), QString("1A2B3C4\n"));
    }

    // --- quill-qt ----------------------------------------------------------------

    void undoIsUserChange()
    {
        auto e = setup(opts(0, true));
        testutil::ChangeRecorder rec(e.get());
        e->insertText(4, "very ", {}, Source::User);
        e->history()->undo();
        QCOMPARE(rec.last().source, Source::User);
        QCOMPARE(rec.last().change.toJson(), delta("[{'retain':4},{'delete':5}]").toJson());
        QCOMPARE(e->history()->redoStack().size(), 1);
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void selectionAfterUndo_data()
    {
        QTest::addColumn<bool>("quillJs");
        QTest::addColumn<int>("undoIndex");
        QTest::addColumn<int>("undoLength");
        QTest::addColumn<int>("redoIndex");
        // flutter_quill: collapsed after the retains and inserts of the step.
        QTest::newRow("flutter_quill") << false << 4 << 0 << 9;
        // Quill.js: the selection the step was recorded with (moved by the redo).
        QTest::newRow("quill.js") << true << 4 << 0 << 9;
    }

    void selectionAfterUndo()
    {
        QFETCH(bool, quillJs);
        QFETCH(int, undoIndex);
        QFETCH(int, undoLength);
        QFETCH(int, redoIndex);
        auto e = setup(opts(0, true));
        if (quillJs)
            e->setEditingRules(QuillEditor::EditingRules::QuillJs);
        e->setSelection(4, 0, Source::User);
        e->insertText(4, "very ", {}, Source::User);
        e->setSelection(0, 0, Source::User);
        e->history()->undo();
        QCOMPARE(e->getSelection(), (quill::Range{undoIndex, undoLength}));
        e->history()->redo();
        QCOMPARE(e->getSelection().index, redoIndex);
    }

    void setContentsClears()
    {
        auto e = setup(opts(0));
        QSignalSpy undo(e.get(), &QTextEdit::undoAvailable);
        e->insertText(0, "x");
        QVERIFY(e->isUndoAvailable());
        QCOMPARE(undo.size(), 1);
        QCOMPARE(undo.last().at(0).toBool(), true);
        e->setContents(Delta().insert("new\n"));
        QVERIFY(!e->isUndoAvailable());
        QCOMPARE(undo.last().at(0).toBool(), false);
    }

    void qtStackIsOff()
    {
        auto e = setup(opts(0));
        QVERIFY(!e->document()->isUndoRedoEnabled());
        e->insertText(0, "x");
        // QTextEdit::undo() through a base pointer: the document stack is off.
        static_cast<QTextEdit*>(e.get())->undo();
        QCOMPARE(e->getText(), QString("xThe lazy fox\n"));
        e->removeHistory();
        QVERIFY(e->document()->isUndoRedoEnabled());
        QVERIFY(!e->isUndoAvailable());
        e->insertText(0, "y");
        e->undo();
        QCOMPARE(e->getText(), QString("xThe lazy fox\n"));
    }

    void readOnly()
    {
        auto e = setup(opts(0));
        e->insertText(0, "x");
        e->setReadOnly(true);
        QVERIFY(!e->history()->undo());
        QCOMPARE(e->getText(), QString("xThe lazy fox\n"));
        QVERIFY(e->history()->canUndo());
    }

    /// Every way to undo, with a remote change in between: only the local
    /// edit goes, the remote text stays.
    void everyPathKeepsRemoteText_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("Ctrl+Z") << "key";
        QTest::newRow("undo()") << "slot";
        QTest::newRow("context menu") << "menu";
        QTest::newRow("toolbar") << "toolbar";
    }

    void everyPathKeepsRemoteText()
    {
        QFETCH(QString, path);
        auto e = setup(opts(400, true), true);
        quill::QuillToolbar toolbar(e.get());
        testutil::ChangeRecorder rec(e.get());
        e->setFocus();
        e->setSelection(12, 0, Source::User);
        QTest::keyClicks(e.get(), "es");
        QCOMPARE(e->getText(), QString("The lazy foxes\n"));
        // A peer's edit arrives, before and after the local text.
        e->updateContents(delta("[{'insert':'[R1] '},{'retain':14},{'insert':' [R2]'}]"), Source::Api);
        QCOMPARE(e->getText(), QString("[R1] The lazy foxes [R2]\n"));
        QVERIFY(toolbar.action("undo")->isEnabled());

        const auto undo = [&](bool redo) {
            if (path == QLatin1String("key")) {
                if (redo)
                    QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
                else
                    QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier);
            } else if (path == QLatin1String("slot")) {
                if (redo)
                    e->redo();
                else
                    e->undo();
            } else if (path == QLatin1String("menu")) {
                const std::unique_ptr<QMenu> menu(quill::QuillEditorPrivate::get(e.get())->contextMenu(QPoint(5, 5)));
                QAction* action = menu->findChild<QAction*>(redo ? "edit-redo" : "edit-undo");
                QVERIFY(action);
                QVERIFY(action->isEnabled());
                action->trigger();
            } else {
                QAction* action = toolbar.action(redo ? "redo" : "undo");
                QVERIFY(action->isEnabled());
                action->trigger();
            }
        };
        undo(false);
        QCOMPARE(e->getText(), QString("[R1] The lazy fox [R2]\n"));
        QVERIFY(!toolbar.action("undo")->isEnabled());
        QVERIFY(toolbar.action("redo")->isEnabled());
        undo(true);
        QCOMPARE(e->getText(), QString("[R1] The lazy foxes [R2]\n"));
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }
};

QTEST_MAIN(TestHistory)
#include "tst_history.moc"
