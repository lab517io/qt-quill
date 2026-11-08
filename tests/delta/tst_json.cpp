// JSON parsing and serialisation in the flutter_quill (bare array) and
// Quill.js ({"ops": [...]}) shapes.
#include "TestUtil.h"

#include <QJsonDocument>
#include <QTest>

using quill::Delta;
using quill::Op;

class TestJson : public QObject
{
    Q_OBJECT

private slots:
    void parsesBothShapes()
    {
        const QByteArray array = R"([{"insert":"Hi","attributes":{"bold":true}},{"insert":"\n"}])";
        const QByteArray object = R"({"ops":[{"insert":"Hi","attributes":{"bold":true}},{"insert":"\n"}]})";
        const auto a = Delta::fromJson(array);
        const auto b = Delta::fromJson(object);
        QVERIFY(a && b);
        QCOMPARE(*a, *b);
        QCOMPARE(*a, Delta().insert("Hi", {{"bold", true}}).insert("\n"));
    }

    void serialisesOpKeyFirst()
    {
        const Delta d = Delta()
                            .insert("Hi", {{"bold", true}, {"color", "#ff0000"}})
                            .insert(QJsonObject{{"image", "a.png"}}, {{"width", "100"}})
                            .insert("\n", {{"header", 1}});
        QCOMPARE(d.toJson(),
                 QByteArray(R"([{"insert":"Hi","attributes":{"bold":true,"color":"#ff0000"}},)"
                            R"({"insert":{"image":"a.png"},"attributes":{"width":"100"}},)"
                            R"({"insert":"\n","attributes":{"header":1}}])"));
        QCOMPARE(Delta().retain(3, {{"bold", QJsonValue::Null}}).remove(2).toJson(Delta::JsonShape::OpsObject),
                 QByteArray(R"({"ops":[{"retain":3,"attributes":{"bold":null}},{"delete":2}]})"));
    }

    void toJsonArrayMatchesToJson()
    {
        const Delta d = Delta().insert("a").retain(2, {{"italic", true}}).remove(1);
        const QJsonDocument doc = QJsonDocument::fromJson(d.toJson());
        QCOMPARE(doc.array(), d.toJsonArray());
        QCOMPARE(d.toJsonObject().value("ops").toArray(), d.toJsonArray());
    }

    void roundTripsUnicode()
    {
        const Delta d = Delta().insert(QStringLiteral("Привіт \U0001F600 \"q\" \\ \t\n"));
        const auto back = Delta::fromJson(d.toJson());
        QVERIFY(back);
        QCOMPARE(*back, d);
        QCOMPARE(back->length(), 18); // UTF-16 units: the emoji counts 2
    }

    void preservesUnknownAttributeValues()
    {
        // flutter_quill custom attributes may hold any JSON value.
        const QByteArray json =
            R"([{"insert":"x","attributes":{"custom":{"a":[1,2,{"b":null}]},"n":1.5,"token":""}},{"insert":"\n"}])";
        const auto d = Delta::fromJson(json);
        QVERIFY(d);
        QCOMPARE(d->toJson(), json);
    }

    void dropsMeaninglessOps()
    {
        const auto d = Delta::fromJson(QByteArray(R"([{"insert":""},{"retain":0},{"delete":0},{"insert":"a"}])"));
        QVERIFY(d);
        QCOMPARE(d->ops().size(), 1);
    }

    void keepsOpsUnmerged()
    {
        // Like `new Delta(ops)`: the ops are taken as they are.
        const auto d = Delta::fromJson(QByteArray(R"([{"insert":"a"},{"insert":"b"}])"));
        QVERIFY(d);
        QCOMPARE(d->ops().size(), 2);
    }

    void rejectsInvalidInput_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("message");
        QTest::newRow("not json") << QByteArray("[{") << "JSON parse error";
        QTest::newRow("scalar root") << QByteArray("42") << "JSON parse error";
        QTest::newRow("object without ops") << QByteArray(R"({"x":[]})") << "expected an array";
        QTest::newRow("op not object") << QByteArray(R"([1])") << "op 0: operation must be an object";
        QTest::newRow("no kind") << QByteArray(R"([{"attributes":{}}])") << "exactly one of";
        QTest::newRow("two kinds") << QByteArray(R"([{"insert":"a","delete":1}])") << "exactly one of";
        QTest::newRow("insert number") << QByteArray(R"([{"insert":1}])") << "\"insert\" must be";
        QTest::newRow("retain fraction") << QByteArray(R"([{"retain":1.5}])") << "non-negative integer";
        QTest::newRow("delete negative") << QByteArray(R"([{"delete":-1}])") << "non-negative integer";
        QTest::newRow("delete string") << QByteArray(R"([{"delete":"1"}])") << "non-negative integer";
        QTest::newRow("attributes array") << QByteArray(R"([{"insert":"a","attributes":[]}])") << "must be an object";
        QTest::newRow("second op bad") << QByteArray(R"([{"insert":"a"},{"retain":true}])") << "op 1:";
    }

    void rejectsInvalidInput()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, message);
        QString error;
        QVERIFY(!Delta::fromJson(json, &error));
        QVERIFY2(error.contains(message), qPrintable(error));
    }

    void flutterQuillExampleDocument()
    {
        const QJsonDocument doc = testutil::loadJson("flutter_quill/example_default_sample.json");
        QString error;
        const auto d = Delta::fromJson(QJsonValue(doc.array()), &error);
        QVERIFY2(d, qPrintable(error));
        QCOMPARE(d->ops().size(), doc.array().size());
        QVERIFY(d->isDocument());
        QVERIFY(d->ops().back().text().endsWith(QLatin1Char('\n')));

        // Re-serialising preserves every op and value; only key order inside
        // each op is normalised (operation first, attributes last).
        const auto back = Delta::fromJson(d->toJson());
        QVERIFY(back);
        QCOMPARE(*back, *d);
        QCOMPARE(QJsonDocument::fromJson(d->toJson()).array(), doc.array());

        // Line structure as Quill sees it.
        int lines = 0;
        int headers = 0;
        d->eachLine([&](const Delta&, const quill::AttributeMap& attrs, qsizetype) {
            ++lines;
            if (attrs.contains("header"))
                ++headers;
            return true;
        });
        QCOMPARE(headers, 3);
        QVERIFY(lines > 30);
    }
};

QTEST_GUILESS_MAIN(TestJson)
#include "tst_json.moc"
