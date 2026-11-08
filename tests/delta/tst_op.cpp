// Hand ports of slab/delta test/op.ts and test/op-iterator.ts. These test
// non-Delta return values, which the call recording does not capture.
#include "TestUtil.h"

#include <quill/OpIterator.h>

#include <QTest>

using quill::AttributeMap;
using quill::Delta;
using quill::Op;
using quill::OpIterator;

class TestOp : public QObject
{
    Q_OBJECT

    Delta m_delta;

private slots:
    void init()
    {
        m_delta = Delta()
                      .insert("Hello", {{"bold", true}})
                      .retain(3)
                      .insert(QJsonObject{{"embed", 2}}, {{"src", "http://quilljs.com/"}})
                      .remove(4);
    }

    // test/op.ts
    void lengthDelete() { QCOMPARE(Op::remove(5).length(), 5); }
    void lengthRetain() { QCOMPARE(Op::retain(2).length(), 2); }
    void lengthInsertText() { QCOMPARE(Op::insert(QStringLiteral("text")).length(), 4); }
    void lengthInsertEmbed() { QCOMPARE(Op::insert(QJsonObject{{"embed", 2}}).length(), 1); }
    void lengthRetainEmbed() { QCOMPARE(Op::retain(QJsonObject{{"delta", QJsonArray()}}).length(), 1); }
    void lengthUtf16()
    {
        // Lengths are UTF-16 code units, as in JavaScript: an emoji counts 2.
        QCOMPARE(Op::insert(QStringLiteral("a\U0001F600b")).length(), 4);
    }

    // test/op-iterator.ts
    void hasNextTrue()
    {
        OpIterator iter(m_delta.ops());
        QVERIFY(iter.hasNext());
    }

    void hasNextFalse()
    {
        const QList<Op> empty;
        OpIterator iter(empty);
        QVERIFY(!iter.hasNext());
    }

    void peekLengthOffsetZero()
    {
        OpIterator iter(m_delta.ops());
        QCOMPARE(iter.peekLength(), 5);
        iter.next();
        QCOMPARE(iter.peekLength(), 3);
        iter.next();
        QCOMPARE(iter.peekLength(), 1);
        iter.next();
        QCOMPARE(iter.peekLength(), 4);
    }

    void peekLengthOffsetPositive()
    {
        OpIterator iter(m_delta.ops());
        iter.next(2);
        QCOMPARE(iter.peekLength(), 5 - 2);
    }

    void peekLengthNoOpsLeft()
    {
        const QList<Op> empty;
        OpIterator iter(empty);
        QCOMPARE(iter.peekLength(), Op::Infinity);
    }

    void peekType()
    {
        OpIterator iter(m_delta.ops());
        QCOMPARE(iter.peekType(), Op::Type::Insert);
        iter.next();
        QCOMPARE(iter.peekType(), Op::Type::Retain);
        iter.next();
        QCOMPARE(iter.peekType(), Op::Type::Insert);
        iter.next();
        QCOMPARE(iter.peekType(), Op::Type::Delete);
        iter.next();
        QCOMPARE(iter.peekType(), Op::Type::Retain);
    }

    void next()
    {
        OpIterator iter(m_delta.ops());
        for (const Op& op : m_delta.ops())
            QVERIFY(iter.next() == op);
        QVERIFY(iter.next() == Op::retain(Op::Infinity));
        QVERIFY(iter.next(4) == Op::retain(Op::Infinity));
        QVERIFY(iter.next() == Op::retain(Op::Infinity));
    }

    void nextLength()
    {
        OpIterator iter(m_delta.ops());
        QVERIFY(iter.next(2) == Op::insert(QStringLiteral("He"), {{"bold", true}}));
        QVERIFY(iter.next(10) == Op::insert(QStringLiteral("llo"), {{"bold", true}}));
        QVERIFY(iter.next(1) == Op::retain(1));
        QVERIFY(iter.next(2) == Op::retain(2));
    }

    void nextZeroMeansAll()
    {
        // JavaScript next(0) is falsy and therefore means "the whole op".
        OpIterator iter(m_delta.ops());
        QVERIFY(iter.next(0) == m_delta.ops().at(0));
    }

    void rest()
    {
        OpIterator iter(m_delta.ops());
        iter.next(2);
        QList<Op> expected{Op::insert(QStringLiteral("llo"), {{"bold", true}}), Op::retain(3),
                           Op::insert(QJsonObject{{"embed", 2}}, {{"src", "http://quilljs.com/"}}),
                           Op::remove(4)};
        QVERIFY(iter.rest() == expected);
        iter.next(3);
        expected.removeFirst();
        QVERIFY(iter.rest() == expected);
        iter.next(3);
        iter.next(2);
        iter.next(4);
        QVERIFY(iter.rest().isEmpty());
    }

    void restKeepsPosition()
    {
        OpIterator iter(m_delta.ops());
        iter.next(2);
        iter.rest();
        QCOMPARE(iter.peekLength(), 3);
    }
};

QTEST_GUILESS_MAIN(TestOp)
#include "tst_op.moc"
