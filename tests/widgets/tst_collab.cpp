// Collaborative editing at the editor level: remote changes against the
// local selection, IME composition and mouse selection; and (below) several
// editors converging through an in-memory OtServer.
#include "EditorTestUtil.h"

#include "Channel.h"
#include "CollabServer.h"
#include "CollabSession.h"

#include <quill/RemoteCursors.h>

#include <QInputMethodEvent>
#include <QRandomGenerator>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

using quill::Delta;
using quill::QuillEditor;
using quill::Range;
using quill::Source;
using testutil::delta;

namespace {

struct Client
{
    std::unique_ptr<QuillEditor> editor;
    collab::CollabSession* session = nullptr;
};

/// N editors connected to one CollabServer over in-memory channels.
struct Room
{
    explicit Room(int n, const Delta& start = Delta().insert("Edit me together.\n")) : server(start) { add(n); }

    void add(int n)
    {
        for (int i = int(clients.size()); n > 0; ++i, --n) {
            Client c;
            c.editor = testutil::makeEditor(false);
            c.session = new collab::CollabSession(c.editor.get(), QStringLiteral("U%1").arg(i),
                                                  QColor::fromHsv(i * 60, 200, 200), c.editor.get());
            c.session->connectTo(channel());
            clients.push_back(std::move(c));
        }
    }

    collab::Channel* channel()
    {
        auto [a, b] = collab::MemoryChannel::pair();
        a->setLatency(latency);
        b->setLatency(latency);
        server.addChannel(b);
        return a;
    }

    bool settled() const
    {
        for (const Client& c : clients) {
            if (!c.session->isConnected() || c.session->client().state() != quill::OtClient::State::Synchronized
                || c.editor->getContents() != server.ot().document())
                return false;
        }
        return true;
    }

    collab::CollabServer server;
    std::vector<Client> clients;
    std::function<int()> latency;
};

/// Characters each editor types: undo must never remove another editor's.
const QStringList kAlphabets{"abcde", "klmno", "vwxyz"};

int countOthers(const QString& text, int self)
{
    int n = 0;
    for (const QChar ch : text) {
        for (int i = 0; i < kAlphabets.size(); ++i) {
            if (i != self && kAlphabets.at(i).contains(ch))
                ++n;
        }
    }
    return n;
}

int rounds()
{
    const int n = qEnvironmentVariableIntValue("QUILLQT_COLLAB_ROUNDS");
    return n > 0 ? n : 12;
}

} // namespace

class TestCollab : public QObject
{
    Q_OBJECT

private slots:
    void cursorsReachPeers_data()
    {
        QTest::addColumn<int>("latency");
        QTest::addColumn<int>("gap");
        QTest::newRow("instant") << 0 << 0;
        QTest::newRow("5 ms, 40 ms apart") << 5 << 40;
        QTest::newRow("30 ms, 10 ms apart") << 30 << 10;
    }

    void cursorsReachPeers()
    {
        QFETCH(int, latency);
        QFETCH(int, gap);
        Room room(0);
        room.latency = [latency] { return latency; };
        room.add(2);
        QTRY_VERIFY(room.settled());
        QuillEditor* ann = room.clients[0].editor.get();
        QuillEditor* bob = room.clients[1].editor.get();
        ann->setSelection(0, 0, Source::User);
        ann->updateContents(Delta().insert("Ann was here. "), Source::User);
        ann->formatText(0, 3, {{"bold", true}}, Source::User);
        ann->insertText(ann->getLength() - 1, "\nLine from Ann", {}, Source::User);
        ann->setSelection(0, 2, Source::User);
        QTest::qWait(gap);
        bob->setSelection(0, 0, Source::User);
        bob->updateContents(Delta().insert("Bob was here. "), Source::User);
        bob->formatText(0, 3, {{"bold", true}}, Source::User);
        bob->insertText(bob->getLength() - 1, "\nLine from Bob", {}, Source::User);
        bob->setSelection(3, 2, Source::User);
        QTRY_VERIFY(room.settled());
        // Ids are given in the order the hellos reach the server.
        const QString annId = room.clients[0].session->clientId();
        const QString bobId = room.clients[1].session->clientId();
        QTRY_VERIFY(ann->remoteCursors()->cursor(bobId).has_value());
        QTRY_COMPARE(ann->remoteCursors()->cursor(bobId)->range, bob->getSelection());
        QTRY_VERIFY(bob->remoteCursors()->cursor(annId).has_value());
        QTRY_COMPARE(bob->remoteCursors()->cursor(annId)->range, ann->getSelection());
    }

    /// Three editors edit at once through the server with random latency:
    /// typing (key events), API edits, formats, undo/redo, going offline.
    /// They must converge, and undo must never remove another editor's text.
    void converges()
    {
        const quint32 base = qEnvironmentVariableIsSet("QUILLQT_COLLAB_SEED")
                                 ? quint32(qEnvironmentVariableIntValue("QUILLQT_COLLAB_SEED"))
                                 : 20261117u;
        int corrections = 0;
        for (int round = 0; round < rounds(); ++round) {
            const quint32 seed = base + quint32(round) * 104729u;
            QRandomGenerator rng(seed);
            const auto below = [&](int n) { return int(rng.bounded(std::max(1, n))); };
            Room room(0);
            room.latency = [&rng] { return int(rng.bounded(25)); };
            room.add(3);
            QTRY_VERIFY(room.settled());
            int undoViolations = 0;
            const int steps = 40 + below(40);
            for (int step = 0; step < steps; ++step) {
                const int who = below(3);
                Client& c = room.clients[who];
                QuillEditor* e = c.editor.get();
                const QString alphabet = kAlphabets.at(who);
                const int len = e->getLength();
                const int action = below(100);
                if (action < 25) {
                    e->setSelection(below(len), 0, Source::User);
                    QString t;
                    for (int i = 1 + below(4); i > 0; --i)
                        t += alphabet.at(below(int(alphabet.size())));
                    QTest::keyClicks(e, t);
                } else if (action < 32) {
                    e->setSelection(below(len), 0, Source::User);
                    QTest::keyClick(e, below(2) ? Qt::Key_Return : Qt::Key_Backspace);
                } else if (action < 42) {
                    QString t = alphabet.left(1 + below(3));
                    if (below(4) == 0)
                        t += QLatin1Char('\n');
                    e->insertText(below(len), t,
                                  below(3) == 0 ? quill::AttributeMap{{"italic", true}} : quill::AttributeMap(),
                                  Source::User);
                } else if (action < 50) {
                    const int at = below(len - 1);
                    e->deleteText(at, 1 + below(std::min(4, len - 1 - at)), Source::User);
                } else if (action < 58) {
                    const int at = below(len - 1);
                    e->formatText(at, 1 + below(5), {{"bold", below(2) ? QJsonValue(true) : QJsonValue()}},
                                  Source::User);
                } else if (action < 63) {
                    static const QList<quill::AttributeMap> lines{
                        {{"header", 1}},
                        {{"list", "bullet"}},
                        {{"list", "ordered"}},
                        {{"blockquote", true}},
                        {{"header", QJsonValue()}, {"list", QJsonValue()}, {"blockquote", QJsonValue()}}};
                    e->formatLine(below(len), 0, lines.at(below(int(lines.size()))), Source::User);
                } else if (action < 75) {
                    const int before = countOthers(e->getText(), who);
                    if (below(3) == 0) {
                        e->redo();
                    } else {
                        e->undo();
                        if (countOthers(e->getText(), who) < before) {
                            ++undoViolations;
                            qWarning("round %d step %d: undo in editor %d removed others' text", round, step, who);
                        }
                    }
                } else if (action < 78) {
                    // Offline for a while, editing on, then back.
                    if (c.session->isConnected())
                        c.session->disconnect();
                    else
                        c.session->reconnect(room.channel());
                } else {
                    QTest::qWait(below(30));
                }
                if (below(4) == 0)
                    QCoreApplication::processEvents();
            }
            for (Client& c : room.clients) {
                if (!c.session->isConnected())
                    c.session->reconnect(room.channel());
            }
            const bool settled = QTest::qWaitFor([&] { return room.settled(); }, 10000);
            if (!settled) {
                qWarning("round %d (seed %u) did not converge; server %s", round, seed,
                         room.server.ot().document().toJson().constData());
                for (const Client& c : room.clients)
                    qWarning("  %s (%d): %s", qPrintable(c.session->clientId()), int(c.session->client().state()),
                             c.editor->getContents().toJson().constData());
            }
            QVERIFY(settled);
            QCOMPARE(undoViolations, 0);
            for (const Client& c : room.clients) {
                QCOMPARE(c.editor->verificationFailures(), 0);
                QCOMPARE(c.session->errors(), 0);
                corrections += c.session->corrections();
            }
        }
        if (corrections > 0)
            qInfo("remote changes corrected after normalisation: %d", corrections);
    }

    void remoteInsertAtCaret()
    {
        auto e = testutil::makeEditor();
        e->setContents(Delta().insert("abcd\n"));
        e->setSelection(2, 0, Source::User);
        e->updateContents(delta("[{'retain':2},{'insert':'XY'}]"), Source::Api);
        // Quill: the caret stays before text inserted at it.
        QCOMPARE(e->getSelection(), (Range{2, 0}));
        e->updateContents(delta("[{'insert':'0'}]"), Source::Api);
        QCOMPARE(e->getSelection(), (Range{3, 0}));
        // A selection keeps its ends; inserts at either end stay outside.
        e->setSelection(3, 2, Source::User);
        e->updateContents(delta("[{'retain':3},{'insert':'<'},{'retain':2},{'insert':'>'}]"), Source::Api);
        QCOMPARE(e->getSelection(), (Range{3, 3}));
        QCOMPARE(e->getText(3, 3), QString("<XY"));
        // The direction of the selection survives.
        QTextCursor c = e->textCursor();
        c.setPosition(8);
        c.setPosition(4, QTextCursor::KeepAnchor);
        e->setTextCursor(c);
        e->updateContents(delta("[{'insert':'!'}]"), Source::Api);
        QCOMPARE(e->textCursor().anchor(), 9);
        QCOMPARE(e->textCursor().position(), 5);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void remoteDeleteOfSelection()
    {
        auto e = testutil::makeEditor();
        e->setContents(Delta().insert("hello world\n"));
        e->setSelection(6, 5, Source::User);
        e->updateContents(delta("[{'retain':4},{'delete':6}]"), Source::Api);
        QCOMPARE(e->getText(), QString("helld\n"));
        QCOMPARE(e->getSelection(), (Range{4, 1}));
        e->updateContents(delta("[{'retain':3},{'delete':3}]"), Source::Api);
        QCOMPARE(e->getSelection(), (Range{3, 0}));
    }

    void remoteChangeDuringComposition()
    {
        auto e = testutil::makeEditor();
        e->setFocus();
        e->setContents(Delta().insert("abc def\n"));
        e->setSelection(7, 0, Source::User);
        QInputMethodEvent preedit(QStringLiteral("xy"), {});
        QApplication::sendEvent(e.get(), &preedit);
        e->updateContents(delta("[{'insert':'123 '}]"), Source::Api);
        e->updateContents(delta("[{'retain':8},{'insert':'!'}]"), Source::Api);
        // The composition moved with the caret (12) and is still open.
        QCOMPARE(e->getSelection(), (Range{12, 0}));
        const QTextBlock block = e->textCursor().block();
        QCOMPARE(block.layout()->preeditAreaText(), QString("xy"));
        QCOMPARE(block.position() + block.layout()->preeditAreaPosition(), 12);
        QCOMPARE(e->getText(), QString("123 abc !def\n")); // preedit text is not content
        // A remote line break before the caret: the composition moves to the new line.
        e->updateContents(delta("[{'retain':4},{'insert':'\\n'}]"), Source::Api);
        QCOMPARE(e->document()->firstBlock().layout()->preeditAreaText(), QString());
        QCOMPARE(e->textCursor().block().layout()->preeditAreaText(), QString("xy"));
        QCOMPARE(e->textCursor().block().layout()->preeditAreaPosition(), 8);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("XY"));
        QApplication::sendEvent(e.get(), &commit);
        QCOMPARE(e->getText(), QString("123 \nabc !defXY\n"));
        QCOMPARE(e->getSelection(), (Range{15, 0}));
        QCOMPARE(e->textCursor().block().layout()->preeditAreaText(), QString());
        QCOMPARE(e->verificationFailures(), 0);
    }

    void remoteChangeDuringMouseSelection()
    {
        auto e = testutil::makeEditor();
        e->setContents(Delta().insert("abcdefghij klmnopqrst\n"));
        const auto pointAt = [&](int pos) {
            QTextCursor c(e->document());
            c.setPosition(pos);
            return e->cursorRect(c).center();
        };
        QTest::mousePress(e->viewport(), Qt::LeftButton, {}, pointAt(2));
        QMouseEvent move(QEvent::MouseMove, pointAt(5), e->viewport()->mapToGlobal(pointAt(5)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e->viewport(), &move);
        QCOMPARE(e->getSelection(), (Range{2, 3}));
        e->updateContents(delta("[{'insert':'0123'}]"), Source::Api);
        QCOMPARE(e->getSelection(), (Range{6, 3}));
        // Dragging on extends from the moved anchor.
        QMouseEvent move2(QEvent::MouseMove, pointAt(14), e->viewport()->mapToGlobal(pointAt(14)), Qt::NoButton,
                          Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(e->viewport(), &move2);
        QTest::mouseRelease(e->viewport(), Qt::LeftButton, {}, pointAt(14));
        QCOMPARE(e->getSelection(), (Range{6, 8}));
        QCOMPARE(e->verificationFailures(), 0);
    }
};

QTEST_MAIN(TestCollab)
#include "tst_collab.moc"
