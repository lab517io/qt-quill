// Replays every Delta / AttributeMap call made by slab/delta's own test-suite
// (recorded by tools/oracle/record-slab-tests.sh) and checks that the C++
// port returns exactly the same result, mutates the receiver the same way,
// and throws the same errors.
#include "TestUtil.h"

#include <quill/Delta.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using quill::AttributeMap;
using quill::Delta;
using quill::Op;

namespace {

bool isMarker(const QJsonValue& v, const char* key)
{
    return v.isObject() && v.toObject().contains(QLatin1String(key));
}
bool isUndefined(const QJsonValue& v) { return isMarker(v, "$u"); }

QJsonValue arg(const QJsonArray& args, int i)
{
    return i < args.size() ? args.at(i) : QJsonValue(QJsonObject{{"$u", 1}});
}

Delta toDelta(const QJsonValue& v)
{
    const QJsonValue ops = isMarker(v, "$delta") ? v.toObject().value("$delta") : v;
    QString error;
    std::optional<Delta> d = Delta::fromJson(ops.isArray() ? ops : QJsonValue(QJsonArray()), &error);
    if (!d)
        qFatal("recorded delta does not parse: %s", qPrintable(error));
    return *d;
}

AttributeMap toAttributes(const QJsonValue& v)
{
    return v.isObject() && !isUndefined(v) ? v.toObject() : AttributeMap();
}

qsizetype toLength(const QJsonValue& v, qsizetype fallback)
{
    if (isUndefined(v))
        return fallback;
    if (isMarker(v, "$inf"))
        return Op::Infinity;
    return qsizetype(v.toDouble());
}

bool toBool(const QJsonValue& v) { return !isUndefined(v) && v.toBool(); }

std::optional<quill::fastdiff::CursorInfo> toCursor(const QJsonValue& v)
{
    if (isUndefined(v) || v.isNull())
        return std::nullopt;
    if (v.isDouble())
        return quill::fastdiff::CursorInfo{{qsizetype(v.toDouble()), 0}, std::nullopt};
    const QJsonObject o = v.toObject();
    const auto range = [](const QJsonValue& r) {
        return quill::fastdiff::Range{qsizetype(r.toObject().value("index").toDouble()),
                                      qsizetype(r.toObject().value("length").toDouble())};
    };
    quill::fastdiff::CursorInfo info{range(o.value("oldRange")), std::nullopt};
    if (o.value("newRange").isObject())
        info.newRange = range(o.value("newRange"));
    return info;
}

QJsonValue deltaHandlerValue(const Delta& d) { return d.toJsonArray(); }

void setHandlers(const QJsonArray& active)
{
    // slab/delta's tests register exactly this handler under "delta".
    bool wantDelta = false;
    for (const QJsonValue& name : active) {
        if (name.toString() == "delta")
            wantDelta = true;
        else
            QFAIL(qPrintable("unexpected embed handler in recording: " + name.toString()));
    }
    if (!wantDelta) {
        Delta::unregisterEmbed("delta");
        return;
    }
    quill::EmbedHandler h;
    h.compose = [](const QJsonValue& a, const QJsonValue& b, bool) {
        return deltaHandlerValue(toDelta(a).compose(toDelta(b)));
    };
    h.transform = [](const QJsonValue& a, const QJsonValue& b, bool priority) {
        return deltaHandlerValue(toDelta(a).transform(toDelta(b), priority));
    };
    h.invert = [](const QJsonValue& a, const QJsonValue& b) {
        return deltaHandlerValue(toDelta(a).invert(toDelta(b)));
    };
    Delta::registerEmbed("delta", h);
}

} // namespace

class TestSlabRecorded : public QObject
{
    Q_OBJECT

    QJsonArray m_records;

private slots:
    void initTestCase()
    {
        const QJsonObject root = testutil::loadJson("delta/slab-delta-recorded.json").object();
        QVERIFY2(root.value("failedSpecs").toArray().isEmpty(),
                 "the recording was made from a failing slab/delta test run");
        m_records = root.value("records").toArray();
        QVERIFY(m_records.size() > 600);
        qInfo("replaying %lld calls from %d slab/delta specs (commit %s)",
              qlonglong(m_records.size()), root.value("specs").toInt(),
              qPrintable(root.value("commit").toString()));
    }

    void replay_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < m_records.size(); ++i) {
            const QJsonObject r = m_records.at(i).toObject();
            const QString name = QStringLiteral("%1 | %2 #%3 %4.%5")
                                     .arg(i)
                                     .arg(r.value("spec").toString())
                                     .arg(r.value("seq").toInt())
                                     .arg(r.value("kind").toString(), r.value("method").toString());
            QTest::newRow(qPrintable(name)) << i;
        }
    }

    void replay()
    {
        QFETCH(int, index);
        const QJsonObject r = m_records.at(index).toObject();
        const QString kind = r.value("kind").toString();
        const QString method = r.value("method").toString();
        const QJsonArray args = r.value("args").toArray();
        const QJsonValue expected = r.value("result");
        const bool expectThrow = r.contains("throws");

        setHandlers(r.value("handlers").toArray());

        if (kind == "attributes") {
            const AttributeMap a = toAttributes(arg(args, 0));
            const AttributeMap b = toAttributes(arg(args, 1));
            AttributeMap actual;
            if (method == "compose")
                actual = quill::attributes::compose(a, b, toBool(arg(args, 2)));
            else if (method == "diff")
                actual = quill::attributes::diff(a, b);
            else if (method == "invert")
                actual = quill::attributes::invert(a, b);
            else if (method == "transform")
                actual = quill::attributes::transform(a, b, toBool(arg(args, 2)));
            else
                QFAIL(qPrintable("unknown method " + method));
            QCOMPARE(actual, toAttributes(expected));
            return;
        }

        Delta self = toDelta(r.value("self"));
        enum class ResultKind { Self, Delta, Number, None } resultKind = ResultKind::None;
        Delta deltaResult;
        qsizetype numberResult = 0;
        int callIndex = 0;
        const QJsonArray calls = r.value("calls").toArray();
        QString thrown;
        try {
            if (method == "insert") {
                const QJsonValue v = arg(args, 0);
                if (v.isString())
                    self.insert(v.toString(), toAttributes(arg(args, 1)));
                else
                    self.insert(v.toObject(), toAttributes(arg(args, 1)));
                resultKind = ResultKind::Self;
            } else if (method == "delete") {
                self.remove(toLength(arg(args, 0), 0));
                resultKind = ResultKind::Self;
            } else if (method == "retain") {
                const QJsonValue v = arg(args, 0);
                if (v.isObject() && !isMarker(v, "$inf"))
                    self.retain(v.toObject(), toAttributes(arg(args, 1)));
                else
                    self.retain(toLength(v, 0), toAttributes(arg(args, 1)));
                resultKind = ResultKind::Self;
            } else if (method == "push") {
                QString error;
                const std::optional<Op> op = Op::fromJson(arg(args, 0), &error);
                QVERIFY2(op, qPrintable(error));
                self.push(*op);
                resultKind = ResultKind::Self;
            } else if (method == "chop") {
                self.chop();
                resultKind = ResultKind::Self;
            } else if (method == "concat") {
                deltaResult = self.concat(toDelta(arg(args, 0)));
                resultKind = ResultKind::Delta;
            } else if (method == "compose") {
                deltaResult = self.compose(toDelta(arg(args, 0)));
                resultKind = ResultKind::Delta;
            } else if (method == "transform") {
                if (arg(args, 0).isDouble()) {
                    numberResult = self.transformPosition(toLength(arg(args, 0), 0), toBool(arg(args, 1)));
                    resultKind = ResultKind::Number;
                } else {
                    deltaResult = self.transform(toDelta(arg(args, 0)), toBool(arg(args, 1)));
                    resultKind = ResultKind::Delta;
                }
            } else if (method == "transformPosition") {
                numberResult = self.transformPosition(toLength(arg(args, 0), 0), toBool(arg(args, 1)));
                resultKind = ResultKind::Number;
            } else if (method == "diff") {
                deltaResult = self.diff(toDelta(arg(args, 0)), toCursor(arg(args, 1)));
                resultKind = ResultKind::Delta;
            } else if (method == "invert") {
                deltaResult = self.invert(toDelta(arg(args, 0)));
                resultKind = ResultKind::Delta;
            } else if (method == "slice") {
                deltaResult = self.slice(toLength(arg(args, 0), 0), toLength(arg(args, 1), Op::Infinity));
                resultKind = ResultKind::Delta;
            } else if (method == "length") {
                numberResult = self.length();
                resultKind = ResultKind::Number;
            } else if (method == "changeLength") {
                numberResult = self.changeLength();
                resultKind = ResultKind::Number;
            } else if (method == "eachLine") {
                const QJsonValue nl = arg(args, 1);
                bool callsOk = true;
                QString mismatch;
                self.eachLine(
                    [&](const Delta& line, const AttributeMap& attrs, qsizetype i) {
                        if (callIndex >= calls.size()) {
                            callsOk = false;
                            mismatch = "more predicate calls than recorded";
                            return false;
                        }
                        const QJsonObject call = calls.at(callIndex++).toObject();
                        const QJsonArray cargs = call.value("args").toArray();
                        if (line != toDelta(cargs.at(0)) || attrs != toAttributes(cargs.at(1))
                            || i != qsizetype(cargs.at(2).toDouble())) {
                            callsOk = false;
                            mismatch = QStringLiteral("call %1: got %2 %3 %4, recorded %5")
                                           .arg(callIndex - 1)
                                           .arg(QString::fromUtf8(line.toJson()),
                                                QString::fromUtf8(QJsonDocument(attrs).toJson(QJsonDocument::Compact)))
                                           .arg(i)
                                           .arg(QString::fromUtf8(QJsonDocument(cargs).toJson(QJsonDocument::Compact)));
                        }
                        const QJsonValue ret = call.value("ret");
                        return !(ret.isBool() && !ret.toBool());
                    },
                    nl.isString() ? nl.toString() : QStringLiteral("\n"));
                QVERIFY2(callsOk, qPrintable(mismatch));
                QCOMPARE(callIndex, int(calls.size()));
                resultKind = ResultKind::None;
            } else {
                QFAIL(qPrintable("unknown method " + method));
            }
        } catch (const quill::DeltaError& e) {
            thrown = QString::fromStdString(e.what());
        }

        if (expectThrow) {
            QCOMPARE(thrown, r.value("throws").toString());
            return;
        }
        QVERIFY2(thrown.isEmpty(), qPrintable("unexpected DeltaError: " + thrown));

        switch (resultKind) {
        case ResultKind::Self:
            QCOMPARE(self, toDelta(r.value("selfAfter")));
            break;
        case ResultKind::Delta:
            QCOMPARE(deltaResult, toDelta(expected));
            break;
        case ResultKind::Number:
            QCOMPARE(numberResult, toLength(expected, -1));
            break;
        case ResultKind::None:
            break;
        }
    }

    void cleanupTestCase() { Delta::unregisterEmbed("delta"); }
};

QTEST_GUILESS_MAIN(TestSlabRecorded)
#include "tst_slab_recorded.moc"
