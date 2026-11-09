// Replays the OT oracle (milestone M8): every case in tests/fixtures/ot/cases/
// evaluated by the C++ port must give exactly what quill-delta gave
// (tests/fixtures/ot/expected/, written by tools/oracle/generate.mjs). When
// the Dart port's results are present (expected-dart/, written by
// tools/dart-oracle), they are compared with quill-delta's as well.
#include "TestUtil.h"

#include <quill/Delta.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using quill::AttributeMap;
using quill::Delta;

namespace {

QJsonDocument readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot read %s", qPrintable(path));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        qFatal("%s: %s", qPrintable(path), qPrintable(error.errorString()));
    return doc;
}

QString otDir(const char* sub)
{
    return testutil::fixturePath(QStringLiteral("ot/") + QLatin1String(sub));
}

QStringList caseFiles()
{
    return QDir(otDir("cases")).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
}

Delta toDelta(const QJsonValue& v)
{
    QString error;
    const auto d = Delta::fromJson(v.isArray() ? v : QJsonValue(QJsonArray()), &error);
    if (!d)
        qFatal("case delta does not parse: %s", qPrintable(error));
    return *d;
}

std::optional<quill::fastdiff::CursorInfo> toCursor(const QJsonValue& v)
{
    if (v.isDouble())
        return quill::fastdiff::CursorInfo{{qsizetype(v.toDouble()), 0}, std::nullopt};
    if (!v.isObject())
        return std::nullopt;
    const auto range = [](const QJsonValue& r) {
        return quill::fastdiff::Range{qsizetype(r[QStringLiteral("index")].toDouble()),
                                      qsizetype(r[QStringLiteral("length")].toDouble())};
    };
    quill::fastdiff::CursorInfo info{range(v[QStringLiteral("oldRange")]), std::nullopt};
    if (v[QStringLiteral("newRange")].isObject())
        info.newRange = range(v[QStringLiteral("newRange")]);
    return info;
}

QJsonValue evaluate(const QJsonObject& c)
{
    const QString op = c.value(QStringLiteral("op")).toString();
    const Delta a = toDelta(c.value(QStringLiteral("a")));
    const Delta b = toDelta(c.value(QStringLiteral("b")));
    const bool priority = c.value(QStringLiteral("priority")).toBool();
    if (op == QLatin1String("compose"))
        return a.compose(b).toJsonArray();
    if (op == QLatin1String("transform"))
        return a.transform(b, priority).toJsonArray();
    if (op == QLatin1String("transformPosition"))
        return double(a.transformPosition(qsizetype(c.value(QStringLiteral("index")).toDouble()), priority));
    if (op == QLatin1String("diff"))
        return a.diff(b, toCursor(c.value(QStringLiteral("cursor")))).toJsonArray();
    if (op == QLatin1String("invert"))
        return a.invert(b).toJsonArray();
    if (op == QLatin1String("slice")) {
        const QJsonValue end = c.value(QStringLiteral("end"));
        return a
            .slice(qsizetype(c.value(QStringLiteral("start")).toDouble()),
                   end.isDouble() ? qsizetype(end.toDouble()) : quill::Op::Infinity)
            .toJsonArray();
    }
    if (op == QLatin1String("concat"))
        return a.concat(b).toJsonArray();
    if (op == QLatin1String("length"))
        return double(a.length());
    if (op == QLatin1String("changeLength"))
        return double(a.changeLength());
    if (op == QLatin1String("eachLine")) {
        QJsonArray lines;
        a.eachLine(
            [&](const Delta& line, const AttributeMap& attrs, qsizetype index) {
                lines.append(QJsonObject{{QStringLiteral("line"), line.toJsonArray()},
                                         {QStringLiteral("attributes"), attrs},
                                         {QStringLiteral("index"), double(index)}});
                return true;
            },
            c.value(QStringLiteral("newline")).toString(QStringLiteral("\n")));
        return lines;
    }
    qFatal("unknown op %s", qPrintable(op));
}

/// {"result": value} or {"error": message}.
QJsonObject run(const QJsonObject& c)
{
    try {
        return QJsonObject{{QStringLiteral("result"), evaluate(c)}};
    } catch (const std::exception& e) {
        return QJsonObject{{QStringLiteral("error"), QString::fromUtf8(e.what())}};
    }
}

QString compact(const QJsonValue& v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

} // namespace

class TestOracle : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void matchesQuillDelta_data()
    {
        QTest::addColumn<QString>("file");
        for (const QString& f : caseFiles())
            QTest::newRow(qPrintable(f)) << f;
    }

    void matchesQuillDelta()
    {
        QFETCH(QString, file);
        const QJsonArray cases = readJson(otDir("cases") + QLatin1Char('/') + file).array();
        const QJsonObject expected = readJson(otDir("expected") + QLatin1Char('/') + file).object();
        QVERIFY(!cases.isEmpty());
        QCOMPARE(expected.size(), cases.size());
        int failures = 0;
        for (const QJsonValue& v : cases) {
            const QJsonObject c = v.toObject();
            const QString name = c.value(QStringLiteral("name")).toString();
            const QJsonObject want = expected.value(name).toObject();
            const QJsonObject got = run(c);
            // Error messages differ between languages; only the fact counts.
            const bool same = want.contains(QStringLiteral("error"))
                                  ? got.contains(QStringLiteral("error"))
                                  : got.value(QStringLiteral("result")) == want.value(QStringLiteral("result"));
            if (!same && ++failures <= 10)
                qWarning("%s: %s\n  case:     %s\n  expected: %s\n  actual:   %s", qPrintable(file), qPrintable(name),
                         qPrintable(compact(c)), qPrintable(compact(want)), qPrintable(compact(got)));
        }
        QCOMPARE(failures, 0);
    }

    void dartPortAgrees_data() { matchesQuillDelta_data(); }

    // dart_quill_delta (flutter_quill) is compared with quill-delta. Results
    // count as agreeing when they are identical, identical once both are
    // canonicalised (Dart does not always merge adjacent inserts or order an
    // insert before a delete), or, for invert, when both undo the change to
    // the same document (Dart omits null removals of attributes the text
    // never had). Anything else must be listed in dart-divergences.json.
    void dartPortAgrees()
    {
        QFETCH(QString, file);
        const QString dartPath = otDir("expected-dart") + QLatin1Char('/') + file;
        if (!QFile::exists(dartPath))
            QSKIP("no Dart results (tools/dart-oracle)");
        const QJsonArray cases = readJson(otDir("cases") + QLatin1Char('/') + file).array();
        const QJsonObject js = readJson(otDir("expected") + QLatin1Char('/') + file).object();
        const QJsonObject dart = readJson(dartPath).object();
        const QJsonObject known = readJson(otDir("dart-divergences.json")).object();
        int exact = 0, canonical = 0, sameEffect = 0, unsupported = 0, listed = 0, failures = 0;
        for (const QJsonValue& v : cases) {
            const QJsonObject c = v.toObject();
            const QString name = c.value(QStringLiteral("name")).toString();
            const QJsonObject want = js.value(name).toObject();
            const QJsonObject got = dart.value(name).toObject();
            if (got.contains(QStringLiteral("unsupported"))) {
                ++unsupported;
                continue;
            }
            const QJsonValue w = want.value(QStringLiteral("result"));
            const QJsonValue g = got.value(QStringLiteral("result"));
            bool same = true;
            if (want.contains(QStringLiteral("error")) || got.contains(QStringLiteral("error"))) {
                same = want.contains(QStringLiteral("error")) == got.contains(QStringLiteral("error"));
                exact += same;
            } else if (w == g) {
                ++exact;
            } else if (w.isArray() && g.isArray() && canonicalise(toDelta(w)) == canonicalise(toDelta(g))) {
                ++canonical;
            } else if (c.value(QStringLiteral("op")).toString() == QLatin1String("invert") && w.isArray()
                       && g.isArray()) {
                const Delta doc = toDelta(c.value(QStringLiteral("b"))).compose(toDelta(c.value(QStringLiteral("a"))));
                same = doc.compose(toDelta(w)) == doc.compose(toDelta(g));
                sameEffect += same;
            } else {
                same = false;
            }
            if (!same && known.contains(name)) {
                ++listed;
                continue;
            }
            if (same && known.contains(name)) {
                qWarning("%s: %s is listed in dart-divergences.json but agrees now", qPrintable(file),
                         qPrintable(name));
                ++failures;
            }
            if (!same && ++failures <= 10)
                qWarning("%s: %s diverges\n  quill-delta: %s\n  dart:        %s", qPrintable(file), qPrintable(name),
                         qPrintable(compact(want)), qPrintable(compact(got)));
        }
        qInfo("%s: %d exact, %d equal when canonical, %d invert with the same effect, %d listed divergences, "
              "%d not supported by dart_quill_delta",
              qPrintable(file), exact, canonical, sameEffect, listed, unsupported);
        QCOMPARE(failures, 0);
    }

private:
    static Delta canonicalise(const Delta& d)
    {
        Delta out;
        for (const quill::Op& op : d.ops())
            out.push(op);
        return out;
    }
};

QTEST_MAIN(TestOracle)
#include "tst_oracle.moc"
