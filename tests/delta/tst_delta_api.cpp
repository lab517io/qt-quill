// Hand ports of the callback-based helpers in slab/delta test/delta/helpers.ts
// plus C++-specific API behaviour (value semantics, errors, embed handlers).
#include "TestUtil.h"

#include <QTest>

using quill::AttributeMap;
using quill::Delta;
using quill::DeltaError;
using quill::Op;
using testutil::delta;

class TestDeltaApi : public QObject
{
    Q_OBJECT

    Delta m_iteration = Delta().insert("Hello").insert(QJsonObject{{"image", true}}).insert("World!");

private slots:
    void filter()
    {
        const QList<Op> arr = m_iteration.filter([](const Op& op, qsizetype) { return op.isTextInsert(); });
        QCOMPARE(arr.size(), 2);
    }

    void forEach()
    {
        int count = 0;
        m_iteration.forEach([&](const Op&, qsizetype) { ++count; });
        QCOMPARE(count, 3);
    }

    void map()
    {
        const QList<QString> arr = m_iteration.map([](const Op& op, qsizetype) { return op.text(); });
        QCOMPARE(arr, (QList<QString>{"Hello", "", "World!"}));
    }

    void partition()
    {
        const auto [passed, failed] = m_iteration.partition([](const Op& op) { return op.isTextInsert(); });
        QVERIFY(passed == (QList<Op>{m_iteration.ops().at(0), m_iteration.ops().at(2)}));
        QVERIFY(failed == (QList<Op>{m_iteration.ops().at(1)}));
    }

    void reduce()
    {
        const qsizetype total =
            m_iteration.reduce([](qsizetype acc, const Op& op, qsizetype) { return acc + op.length(); }, qsizetype(0));
        QCOMPARE(total, m_iteration.length());
    }

    void eachLineEarlyReturn()
    {
        const Delta d = Delta().insert("Hello\nNew\nWorld!");
        int calls = 0;
        int count = 0;
        d.eachLine([&](const Delta&, const AttributeMap&, qsizetype) {
            ++calls;
            if (count == 1)
                return false;
            count += 1;
            return true;
        });
        QCOMPARE(calls, 2);
    }

    void operationsAreConstAndValueTyped()
    {
        const Delta a = delta(R"([{"insert":"A","attributes":{"bold":true}}])");
        const Delta b = delta(R"([{"retain":1,"attributes":{"color":"red"}}])");
        const Delta aCopy = a;
        const Delta bCopy = b;
        a.compose(b);
        a.transform(b, true);
        b.invert(a);
        QCOMPARE(a, aCopy);
        QCOMPARE(b, bCopy);
    }

    void constructorKeepsOpsUnmerged()
    {
        // Like `new Delta(ops)`: no merging on construction.
        const Delta d(QList<Op>{Op::insert("a"), Op::insert("b")});
        QCOMPARE(d.ops().size(), 2);
        QCOMPARE(Delta().push(Op::insert("a")).push(Op::insert("b")).ops().size(), 1);
    }

    void isDocument()
    {
        QVERIFY(delta(R"([{"insert":"a\n"}])").isDocument());
        QVERIFY(Delta().isDocument());
        QVERIFY(!delta(R"([{"retain":1}])").isDocument());
    }

    void diffErrors()
    {
        const Delta doc = Delta().insert("a");
        const Delta change = Delta().retain(1);
        QVERIFY_THROWS_EXCEPTION(DeltaError, doc.diff(change));
        try {
            change.diff(doc);
            QFAIL("expected DeltaError");
        } catch (const DeltaError& e) {
            QCOMPARE(QString::fromUtf8(e.what()), QStringLiteral("diff() called with non-document"));
        }
        try {
            doc.diff(change);
            QFAIL("expected DeltaError");
        } catch (const DeltaError& e) {
            QCOMPARE(QString::fromUtf8(e.what()), QStringLiteral("diff() called on non-document"));
        }
    }

    void diffCursorHint()
    {
        // Without a hint the inserted "a" is attributed to the end; the
        // cursor hint makes it an insert at index 1 (Quill behaviour).
        const Delta a = Delta().insert("aa\n");
        const Delta b = Delta().insert("aaa\n");
        QCOMPARE(a.diff(b), delta(R"([{"retain":2},{"insert":"a"}])"));
        QCOMPARE(a.diff(b, qsizetype(1)), delta(R"([{"retain":1},{"insert":"a"}])"));
    }

    void diffWithEmojiDoesNotSplitSurrogates()
    {
        const Delta a = Delta().insert(QStringLiteral("\U0001F600\n"));
        const Delta b = Delta().insert(QStringLiteral("\U0001F601\n"));
        const Delta d = a.diff(b);
        QCOMPARE(a.compose(d), b);
        QCOMPARE(d, Delta().insert(QStringLiteral("\U0001F601")).remove(2));
    }

    void embedRetainWithoutHandlerThrows()
    {
        const Delta a = Delta().insert(QJsonObject{{"mydelta", QJsonArray{QJsonObject{{"insert", "a"}}}}});
        const Delta b = Delta().retain(QJsonObject{{"mydelta", QJsonArray{QJsonObject{{"insert", "b"}}}}});
        try {
            a.compose(b);
            QFAIL("expected DeltaError");
        } catch (const DeltaError& e) {
            QCOMPARE(QString::fromUtf8(e.what()), QStringLiteral("no handlers for embed type \"mydelta\""));
        }
    }

    void embedHandlerRegistration()
    {
        quill::EmbedHandler counter;
        counter.compose = [](const QJsonValue& a, const QJsonValue& b, bool) {
            return QJsonValue(a.toInt() + b.toInt());
        };
        counter.invert = [](const QJsonValue& a, const QJsonValue&) { return QJsonValue(-a.toInt()); };
        counter.transform = [](const QJsonValue&, const QJsonValue& b, bool) { return b; };
        Delta::registerEmbed("counter", counter);
        const Delta doc = Delta().insert(QJsonObject{{"counter", 1}});
        const Delta change = Delta().retain(QJsonObject{{"counter", 2}});
        QCOMPARE(doc.compose(change), Delta().insert(QJsonObject{{"counter", 3}}));
        QCOMPARE(change.invert(doc), Delta().retain(QJsonObject{{"counter", -2}}));
        Delta::unregisterEmbed("counter");
        QVERIFY_THROWS_EXCEPTION(DeltaError, doc.compose(change));
    }

    void infinitySliceDefaults()
    {
        const Delta d = Delta().insert("0123456789");
        QCOMPARE(d.slice(), d);
        QCOMPARE(d.slice(8), Delta().insert("89"));
    }
};

QTEST_GUILESS_MAIN(TestDeltaApi)
#include "tst_delta_api.moc"
