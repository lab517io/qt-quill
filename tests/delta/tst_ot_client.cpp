// OtClient / OtServer: the client state machine (after ot.js's client
// tests), the server's revision log, and a simulated network with K clients,
// random delays, disconnects and resends that must always converge.
#include "TestUtil.h"

#include <quill/OtClient.h>
#include <quill/OtServer.h>

#include <QRandomGenerator>
#include <QTest>

#include <deque>

using quill::AttributeMap;
using quill::Delta;
using quill::OtClient;
using quill::OtServer;
using testutil::delta;

namespace {

/// Random edits that keep a document valid: nothing after the final
/// newline, and the final newline stays (as an editor makes them).
class Gen
{
public:
    explicit Gen(quint32 seed) : m_rng(seed) {}

    int below(int n) { return int(m_rng.bounded(n)); }
    bool chance(int percent) { return below(100) < percent; }

    QString text()
    {
        static const QStringList pieces{"a", "b", "c", " ", "\n", QStringLiteral("\U0001F600"), "xy"};
        QString s;
        const int n = 1 + below(3);
        for (int i = 0; i < n; ++i)
            s += pieces.at(below(int(pieces.size())));
        return s;
    }

    AttributeMap attributes(bool allowNull)
    {
        AttributeMap a;
        if (chance(30))
            a.insert("bold", allowNull && chance(30) ? QJsonValue(QJsonValue::Null) : QJsonValue(true));
        if (chance(15))
            a.insert("color",
                     allowNull && chance(30) ? QJsonValue(QJsonValue::Null) : QJsonValue(chance(50) ? "red" : "blue"));
        return a;
    }

    Delta change(const Delta& document)
    {
        const qsizetype length = document.length() - 1; // the final "\n" stays
        Delta d;
        qsizetype pos = 0;
        // Edits are mostly small and local, like typing.
        if (length > 0 && chance(70)) {
            pos = below(int(length + 1));
            d.retain(pos);
        }
        const int ops = 1 + below(3);
        for (int i = 0; i < ops; ++i) {
            const int kind = below(10);
            const qsizetype left = length - pos;
            if (kind < 5 || left == 0) {
                if (chance(10))
                    d.insert(QJsonObject{{"image", "c.png"}}, attributes(false));
                else
                    d.insert(text(), attributes(false));
            } else if (kind < 8) {
                const qsizetype n = 1 + below(int(std::min<qsizetype>(left, 3)));
                d.remove(n);
                pos += n;
            } else {
                const qsizetype n = 1 + below(int(std::min<qsizetype>(left, 4)));
                d.retain(n, attributes(true));
                pos += n;
            }
        }
        if (pos == length && chance(10))
            d.retain(1, AttributeMap{{"header", chance(50) ? QJsonValue(1) : QJsonValue(QJsonValue::Null)}});
        return d.chop();
    }

private:
    QRandomGenerator m_rng;
};

struct Message
{
    enum Kind { Op, Ack } kind = Op;
    qint64 revision = 0;
    Delta change;
    QString clientId;
    qint64 sequence = 0;
};

struct Peer
{
    QString id;
    Delta document;
    OtClient client;
    std::deque<Message> up;   // to the server
    std::deque<Message> down; // from the server
    bool connected = true;
};

/// K clients and a server connected by FIFO channels.
class Network
{
public:
    Network(int clients, const Delta& start) : server(start)
    {
        for (int i = 0; i < clients; ++i) {
            auto p = std::make_unique<Peer>();
            p->id = QStringLiteral("c%1").arg(i);
            p->document = start;
            Peer* peer = p.get();
            p->client.send = [peer](qint64 rev, const Delta& change) {
                peer->up.push_back({Message::Op, rev, change, peer->id, peer->client.sequence()});
            };
            p->client.apply = [peer](const Delta& change) { peer->document = peer->document.compose(change); };
            peers.push_back(std::move(p));
        }
    }

    void edit(Peer& p, const Delta& change)
    {
        p.document = p.document.compose(change);
        p.client.applyClient(change);
    }

    /// Delivers the oldest message from `p` to the server.
    bool deliverUp(Peer& p)
    {
        if (p.up.empty() || !p.connected)
            return false;
        const Message m = p.up.front();
        p.up.pop_front();
        const OtServer::Result r = server.receive(m.revision, m.change, m.clientId, m.sequence);
        if (r.status == OtServer::Result::Status::Rejected) {
            ++rejected;
            qWarning("rejected: %s", qPrintable(r.error));
            return true;
        }
        if (r.status == OtServer::Result::Status::Duplicate) {
            ++duplicates;
            return true;
        }
        for (auto& other : peers) {
            if (!other->connected)
                continue;
            if (other.get() == &p)
                other->down.push_back({Message::Ack, r.revision, {}, m.clientId, m.sequence});
            else
                other->down.push_back({Message::Op, r.revision, r.change, m.clientId, m.sequence});
        }
        return true;
    }

    bool deliverDown(Peer& p)
    {
        if (p.down.empty() || !p.connected)
            return false;
        const Message m = p.down.front();
        p.down.pop_front();
        if (m.kind == Message::Ack)
            p.client.serverAck();
        else
            p.client.applyServer(m.change);
        if (p.client.revision() != m.revision) {
            ++revisionErrors;
            qWarning("%s is at revision %lld, message was %lld", qPrintable(p.id), p.client.revision(), m.revision);
        }
        return true;
    }

    /// The connection drops: what was in flight is lost, except that the
    /// server may already have read the client's messages.
    void disconnect(Peer& p, bool serverReadUp)
    {
        if (serverReadUp)
            while (deliverUp(p)) {
            }
        p.connected = false;
        p.up.clear();
        p.down.clear();
    }

    /// Reconnects: the server replays what the client missed, the client
    /// resends its change in flight if the replay did not acknowledge it.
    void reconnect(Peer& p)
    {
        p.connected = true;
        const qint64 before = p.client.revision();
        for (const OtServer::Entry& e : server.entriesSince(before)) {
            if (e.clientId == p.id && p.client.outstanding() && e.sequence == p.client.sequence())
                p.client.serverAck();
            else
                p.client.applyServer(e.change);
        }
        if (p.client.revision() != server.revision())
            ++revisionErrors;
        p.client.serverReconnect();
    }

    void drain()
    {
        for (auto& p : peers) {
            if (!p->connected)
                reconnect(*p);
        }
        bool moved = true;
        while (moved) {
            moved = false;
            for (auto& p : peers) {
                while (deliverUp(*p))
                    moved = true;
                while (deliverDown(*p))
                    moved = true;
            }
        }
    }

    OtServer server;
    std::vector<std::unique_ptr<Peer>> peers;
    int rejected = 0;
    int duplicates = 0;
    int revisionErrors = 0;
};

quint32 baseSeed()
{
    bool ok = false;
    const quint32 s = qEnvironmentVariable("QUILLQT_OT_SEED").toUInt(&ok);
    return ok ? s : 20261117u;
}

int runs()
{
    const int n = qEnvironmentVariableIntValue("QUILLQT_OT_RUNS");
    return n > 0 ? n : 2000;
}

} // namespace

class TestOtClient : public QObject
{
    Q_OBJECT

private slots:
    // --- State machine (ot.js test/client.js) ---------------------------------

    void synchronizedSendsAtOnce()
    {
        OtClient c(1);
        QList<std::pair<qint64, Delta>> sent;
        c.send = [&](qint64 rev, const Delta& d) { sent.append({rev, d}); };
        QCOMPARE(c.state(), OtClient::State::Synchronized);
        c.applyClient(delta("[{'insert':'a'}]"));
        QCOMPARE(c.state(), OtClient::State::AwaitingConfirm);
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.at(0).first, 1);
        QCOMPARE(c.sequence(), 1);
    }

    void buffersWhileAwaiting()
    {
        OtClient c(1);
        int sent = 0;
        c.send = [&](qint64, const Delta&) { ++sent; };
        c.applyClient(delta("[{'insert':'a'}]"));
        c.applyClient(delta("[{'retain':1},{'insert':'b'}]"));
        QCOMPARE(c.state(), OtClient::State::AwaitingWithBuffer);
        c.applyClient(delta("[{'retain':2},{'insert':'c'}]"));
        QCOMPARE(sent, 1);
        QCOMPARE(testutil::str(*c.buffer()), QByteArray(R"([{"retain":1},{"insert":"bc"}])"));
    }

    void ackSendsBuffer()
    {
        OtClient c(1);
        QList<std::pair<qint64, Delta>> sent;
        c.send = [&](qint64 rev, const Delta& d) { sent.append({rev, d}); };
        c.applyClient(delta("[{'insert':'a'}]"));
        c.applyClient(delta("[{'retain':1},{'insert':'b'}]"));
        c.serverAck();
        QCOMPARE(c.revision(), 2);
        QCOMPARE(c.state(), OtClient::State::AwaitingConfirm);
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent.at(1).first, 2);
        QCOMPARE(c.sequence(), 2);
        c.serverAck();
        QCOMPARE(c.state(), OtClient::State::Synchronized);
        QCOMPARE(c.revision(), 3);
        c.serverAck(); // stray: ignored
        QCOMPARE(c.revision(), 3);
    }

    void serverChangeWhenSynchronized()
    {
        OtClient c(4);
        QList<Delta> applied;
        c.apply = [&](const Delta& d) { applied.append(d); };
        c.applyServer(delta("[{'insert':'x'}]"));
        QCOMPARE(c.revision(), 5);
        QCOMPARE(applied.size(), 1);
        QCOMPARE(testutil::str(applied.at(0)), QByteArray(R"([{"insert":"x"}])"));
    }

    void serverChangeIsTransformed()
    {
        // Both insert at 0: the server's insert goes first.
        OtClient c(0);
        Delta local = Delta().insert("ab\n");
        QList<Delta> applied;
        c.send = [](qint64, const Delta&) {};
        c.apply = [&](const Delta& d) { local = local.compose(d); };
        const Delta mine = delta("[{'insert':'L'}]");
        local = local.compose(mine);
        c.applyClient(mine);
        const Delta buffered = delta("[{'retain':3},{'insert':'M'}]");
        local = local.compose(buffered);
        c.applyClient(buffered);
        c.applyServer(delta("[{'insert':'S'},{'retain':1},{'delete':1}]"));
        QCOMPARE(testutil::str(local), QByteArray(R"([{"insert":"SLaM\n"}])"));
        QCOMPARE(testutil::str(*c.outstanding()), QByteArray(R"([{"retain":1},{"insert":"L"}])"));
        QCOMPARE(testutil::str(*c.buffer()), QByteArray(R"([{"retain":3},{"insert":"M"}])"));
    }

    void positionsThroughPendingChanges()
    {
        OtClient c(0);
        c.send = [](qint64, const Delta&) {};
        c.applyClient(delta("[{'retain':2},{'insert':'xyz'}]"));
        c.applyClient(delta("[{'delete':1}]"));
        // Local "?b xyz ..." comes from server "ab..."; pending: insert 3 at 2, then delete 1 at 0.
        QCOMPARE(c.transformFromServer(0), 0);
        QCOMPARE(c.transformFromServer(4), 6);
        QCOMPARE(c.transformToServer(6), 4);
        QCOMPARE(c.transformToServer(2), 2); // inside the pending insert: where it was inserted
        QCOMPARE(c.transformToServer(0), 1);
    }

    // --- Server ----------------------------------------------------------------

    void serverTransformsAgainstLog()
    {
        OtServer s(Delta().insert("abc\n"));
        auto r1 = s.receive(0, delta("[{'insert':'1'}]"), "a", 1);
        QCOMPARE(r1.status, OtServer::Result::Status::Applied);
        QCOMPARE(r1.revision, 1);
        // Based on revision 0: also inserts at 0, goes after the earlier one.
        auto r2 = s.receive(0, delta("[{'insert':'2'},{'retain':3}]"), "b", 1);
        QCOMPARE(r2.status, OtServer::Result::Status::Applied);
        QCOMPARE(testutil::str(r2.change), QByteArray(R"([{"retain":1},{"insert":"2"}])"));
        QCOMPARE(testutil::str(s.document()), QByteArray(R"([{"insert":"12abc\n"}])"));
        QCOMPARE(s.entriesSince(1).size(), 1);
        QCOMPARE(s.entriesSince(2).size(), 0);
    }

    void serverRejectsAndDedupes()
    {
        OtServer s(Delta().insert("ab\n"));
        QCOMPARE(s.receive(5, delta("[{'insert':'x'}]")).status, OtServer::Result::Status::Rejected);
        QCOMPARE(s.receive(0, delta("[{'retain':9},{'insert':'x'}]")).status, OtServer::Result::Status::Rejected);
        QCOMPARE(s.receive(0, delta("[{'retain':2},{'delete':1}]")).status, OtServer::Result::Status::Rejected);
        QCOMPARE(s.receive(0, delta("[{'retain':3},{'insert':'x'}]")).status, OtServer::Result::Status::Rejected);
        QCOMPARE(s.revision(), 0);
        QCOMPARE(s.receive(0, delta("[{'insert':'x'}]"), "a", 1).status, OtServer::Result::Status::Applied);
        QCOMPARE(s.receive(0, delta("[{'insert':'x'}]"), "a", 1).status, OtServer::Result::Status::Duplicate);
        QCOMPARE(s.receive(1, delta("[{'insert':'y'}]"), "a", 2).status, OtServer::Result::Status::Applied);
        QCOMPARE(testutil::str(s.document()), QByteArray(R"([{"insert":"yxab\n"}])"));
    }

    // --- Simulated network ------------------------------------------------------

    void converges_data()
    {
        QTest::addColumn<int>("clients");
        QTest::addColumn<bool>("disconnects");
        QTest::newRow("2 clients") << 2 << false;
        QTest::newRow("3 clients") << 3 << false;
        QTest::newRow("5 clients") << 5 << false;
        QTest::newRow("3 clients, disconnects") << 3 << true;
    }

    void converges()
    {
        QFETCH(int, clients);
        QFETCH(bool, disconnects);
        const int n = runs();
        for (int run = 0; run < n; ++run) {
            const quint32 seed = baseSeed() + quint32(run) * 7919u + quint32(clients) * 131u + (disconnects ? 17u : 0u);
            Gen gen(seed);
            Network net(clients, Delta().insert("ab c\n"));
            const int steps = 20 + gen.below(80);
            for (int step = 0; step < steps; ++step) {
                Peer& p = *net.peers.at(gen.below(clients));
                const int action = gen.below(100);
                if (action < 40) {
                    net.edit(p, gen.change(p.document));
                } else if (action < 65) {
                    net.deliverUp(p);
                } else if (action < 95) {
                    net.deliverDown(p);
                } else if (disconnects) {
                    if (p.connected)
                        net.disconnect(p, gen.chance(50));
                    else
                        net.reconnect(p);
                }
            }
            net.drain();
            bool ok = net.rejected == 0 && net.revisionErrors == 0;
            for (const auto& p : net.peers) {
                ok = ok && p->document == net.server.document() && p->client.state() == OtClient::State::Synchronized
                     && p->client.revision() == net.server.revision();
            }
            if (!ok) {
                qWarning("run %d (seed %u) diverged; server %s", run, seed, net.server.document().toJson().constData());
                for (const auto& p : net.peers)
                    qWarning("  %s: %s", qPrintable(p->id), p->document.toJson().constData());
                QFAIL("clients did not converge");
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestOtClient)
#include "tst_ot_client.moc"
