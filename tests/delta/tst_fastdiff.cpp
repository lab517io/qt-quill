// Checks the fast-diff port against outputs recorded from fast-diff 1.3.0
// (tools/oracle/record-fastdiff.mjs).
#include "TestUtil.h"

#include <quill/FastDiff.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace quill::fastdiff;

namespace {

QString render(const QList<Diff>& diffs)
{
    QString s;
    for (const Diff& d : diffs) {
        s += d.first == Operation::Insert ? QLatin1Char('+')
             : d.first == Operation::Delete ? QLatin1Char('-')
                                            : QLatin1Char('=');
        s += QString(d.second).replace(QChar(u'\0'), QStringLiteral("\\0"));
        s += QLatin1Char('|');
    }
    return s;
}

} // namespace

class TestFastDiff : public QObject
{
    Q_OBJECT

    QJsonArray m_cases;

private slots:
    void initTestCase()
    {
        const QJsonObject root = testutil::loadJson("delta/fastdiff-cases.json").object();
        QCOMPARE(root.value("fastDiffVersion").toString(), QStringLiteral("1.3.0"));
        m_cases = root.value("cases").toArray();
        QVERIFY(m_cases.size() > 1000);
    }

    void recorded_data()
    {
        QTest::addColumn<int>("index");
        for (int i = 0; i < m_cases.size(); ++i) {
            const QJsonObject c = m_cases.at(i).toObject();
            QTest::addRow("%d %s%s", i, qPrintable(c.value("group").toString()),
                          c.value("cleanup").toBool() ? " cleanup" : "")
                << i;
        }
    }

    void recorded()
    {
        QFETCH(int, index);
        const QJsonObject c = m_cases.at(index).toObject();
        std::optional<CursorInfo> cursor;
        const QJsonValue cv = c.value("cursor");
        if (cv.isDouble()) {
            cursor = CursorInfo{{qsizetype(cv.toDouble()), 0}, std::nullopt};
        } else if (cv.isObject()) {
            const auto range = [](const QJsonValue& r) {
                return Range{qsizetype(r.toObject().value("index").toDouble()),
                             qsizetype(r.toObject().value("length").toDouble())};
            };
            cursor = CursorInfo{range(cv.toObject().value("oldRange")), std::nullopt};
            if (cv.toObject().value("newRange").isObject())
                cursor->newRange = range(cv.toObject().value("newRange"));
        }

        QList<Diff> expected;
        for (const QJsonValue& d : c.value("result").toArray()) {
            const QJsonArray pair = d.toArray();
            expected.append({Operation(pair.at(0).toInt()), pair.at(1).toString()});
        }
        const QList<Diff> actual =
            diff(c.value("a").toString(), c.value("b").toString(), cursor, c.value("cleanup").toBool());
        QCOMPARE(render(actual), render(expected));
    }
};

QTEST_GUILESS_MAIN(TestFastDiff)
#include "tst_fastdiff.moc"
