// Seeded property tests for the algebraic laws Quill relies on. The seeds are
// fixed so failures are reproducible; set QUILLQT_PROPERTY_SEED to explore.
#include "TestUtil.h"

#include <QRandomGenerator>
#include <QTest>

using quill::AttributeMap;
using quill::Delta;

namespace {

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
        const int n = 1 + below(4);
        for (int i = 0; i < n; ++i)
            s += pieces.at(below(int(pieces.size())));
        return s;
    }

    AttributeMap attributes(bool allowNull)
    {
        AttributeMap a;
        if (chance(30))
            a.insert("bold", allowNull && chance(30) ? QJsonValue(QJsonValue::Null) : QJsonValue(true));
        if (chance(20))
            a.insert("color",
                     allowNull && chance(30) ? QJsonValue(QJsonValue::Null) : QJsonValue(chance(50) ? "red" : "blue"));
        if (chance(10))
            a.insert("header", allowNull && chance(30) ? QJsonValue(QJsonValue::Null) : QJsonValue(1 + below(3)));
        return a;
    }

    Delta document()
    {
        Delta d;
        const int n = below(6);
        for (int i = 0; i < n; ++i) {
            if (chance(15))
                d.insert(QJsonObject{{"image", chance(50) ? "a.png" : "b.png"}}, attributes(false));
            else
                d.insert(text(), attributes(false));
        }
        d.insert("\n");
        return d;
    }

    /// A random change applicable to a document of `length`.
    Delta change(qsizetype length)
    {
        Delta d;
        qsizetype pos = 0;
        while (pos < length) {
            const int kind = below(10);
            const qsizetype n = 1 + below(int(std::min<qsizetype>(length - pos, 4)));
            if (kind < 4) {
                d.retain(n, chance(40) ? attributes(true) : AttributeMap());
                pos += n;
            } else if (kind < 6) {
                d.remove(n);
                pos += n;
            } else if (kind < 9) {
                if (chance(15))
                    d.insert(QJsonObject{{"image", "c.png"}}, attributes(false));
                else
                    d.insert(text(), attributes(false));
            } else {
                break;
            }
        }
        if (chance(30))
            d.insert(text());
        return d.chop();
    }

private:
    QRandomGenerator m_rng;
};

quint32 baseSeed()
{
    bool ok = false;
    const quint32 s = qEnvironmentVariable("QUILLQT_PROPERTY_SEED").toUInt(&ok);
    return ok ? s : 20261108u;
}

constexpr int kIterations = 2000;

} // namespace

class TestProperties : public QObject
{
    Q_OBJECT

private slots:
    void diffThenComposeYieldsTarget()
    {
        Gen g(baseSeed());
        for (int i = 0; i < kIterations; ++i) {
            const Delta a = g.document();
            const Delta b = g.document();
            const Delta d = a.diff(b);
            QVERIFY2(a.compose(d) == b,
                     qPrintable(QStringLiteral("iteration %1: a=%2 b=%3 diff=%4")
                                    .arg(i)
                                    .arg(QString::fromUtf8(a.toJson()), QString::fromUtf8(b.toJson()),
                                         QString::fromUtf8(d.toJson()))));
        }
    }

    void composeWithChangeKeepsDocument()
    {
        Gen g(baseSeed() + 1);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const Delta change = g.change(doc.length());
            const Delta result = doc.compose(change);
            QVERIFY(result.isDocument());
            QCOMPARE(result.length(), doc.length() + change.changeLength());
        }
    }

    void invertUndoesChange()
    {
        Gen g(baseSeed() + 2);
        for (int i = 0; i < kIterations; ++i) {
            const Delta base = g.document();
            const Delta change = g.change(base.length());
            const Delta inverted = change.invert(base);
            QVERIFY2(base.compose(change).compose(inverted) == base,
                     qPrintable(QStringLiteral("iteration %1: base=%2 change=%3 inverted=%4")
                                    .arg(i)
                                    .arg(QString::fromUtf8(base.toJson()), QString::fromUtf8(change.toJson()),
                                         QString::fromUtf8(inverted.toJson()))));
        }
    }

    void transformConverges()
    {
        // TP1: doc ∘ a ∘ a'(b) == doc ∘ b ∘ b'(a), with a having priority.
        Gen g(baseSeed() + 3);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const Delta a = g.change(doc.length());
            const Delta b = g.change(doc.length());
            const Delta left = doc.compose(a).compose(a.transform(b, true));
            const Delta right = doc.compose(b).compose(b.transform(a, false));
            QVERIFY2(left == right, qPrintable(QStringLiteral("iteration %1: doc=%2 a=%3 b=%4 left=%5 right=%6")
                                                   .arg(i)
                                                   .arg(QString::fromUtf8(doc.toJson()), QString::fromUtf8(a.toJson()),
                                                        QString::fromUtf8(b.toJson()), QString::fromUtf8(left.toJson()),
                                                        QString::fromUtf8(right.toJson()))));
        }
    }

    void transformPositionAgreesWithTransform()
    {
        // A cursor moved by transformPosition lands where an insert at that
        // cursor lands after transform(). The priority flags are opposite:
        // transformPosition(i, true) keeps the cursor before a concurrent
        // insert, transform(x, true) lets this delta's insert go first.
        const auto landing = [](const Delta& moved) {
            return moved.ops().isEmpty() || !moved.ops().front().isCountRetain() ? qsizetype(0)
                                                                                 : moved.ops().front().count();
        };
        Gen g(baseSeed() + 4);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const Delta a = g.change(doc.length());
            const qsizetype index = g.below(int(doc.length()));
            const Delta insertAt = Delta().retain(index).insert("|");
            QCOMPARE(a.transformPosition(index, false), landing(a.transform(insertAt, true)));
            QCOMPARE(a.transformPosition(index, true), landing(a.transform(insertAt, false)));
        }
    }

    void composeIsAssociative()
    {
        Gen g(baseSeed() + 5);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const Delta a = g.change(doc.length());
            const Delta b = g.change(doc.compose(a).length());
            QCOMPARE(doc.compose(a).compose(b), doc.compose(a.compose(b)));
        }
    }

    void jsonRoundTrip()
    {
        Gen g(baseSeed() + 6);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const Delta change = g.change(doc.length());
            for (const Delta& d : {doc, change}) {
                QCOMPARE(*Delta::fromJson(d.toJson()), d);
                QCOMPARE(*Delta::fromJson(d.toJson(Delta::JsonShape::OpsObject)), d);
            }
        }
    }

    void sliceConcatIdentity()
    {
        Gen g(baseSeed() + 7);
        for (int i = 0; i < kIterations; ++i) {
            const Delta doc = g.document();
            const qsizetype cut = g.below(int(doc.length() + 1));
            QCOMPARE(doc.slice(0, cut).concat(doc.slice(cut)).compose(Delta()), doc.compose(Delta()));
            QCOMPARE(doc.slice(0, cut).length() + doc.slice(cut).length(), doc.length());
        }
    }
};

QTEST_GUILESS_MAIN(TestProperties)
#include "tst_properties.moc"
