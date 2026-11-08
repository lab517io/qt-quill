#include "TestUtil.h"

#include <quill/Colors.h>

#include <QTest>

using quill::ColorDialect;
namespace colors = quill::colors;

Q_DECLARE_METATYPE(ColorDialect)

class TestColors : public QObject
{
    Q_OBJECT

private slots:
    void parse_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<ColorDialect>("dialect");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("argb"); // expected, as #AARRGGBB

        const auto F = ColorDialect::Flutter;
        const auto C = ColorDialect::Css;
        QTest::newRow("hex6") << "#ff0000" << F << true << "#ffff0000";
        QTest::newRow("hex6 upper") << "#00FF7F" << C << true << "#ff00ff7f";
        QTest::newRow("hex3") << "#0f8" << F << true << "#ff00ff88";
        QTest::newRow("hex8 flutter argb") << "#80FF0000" << F << true << "#80ff0000";
        QTest::newRow("hex8 css rgba") << "#FF000080" << C << true << "#80ff0000";
        QTest::newRow("flutter red") << "red" << F << true << "#fff44336";
        QTest::newRow("css red") << "red" << C << true << "#ffff0000";
        QTest::newRow("flutter black54") << "black54" << F << true << "#8a000000";
        QTest::newRow("flutter-only name in css") << "redAccent" << C << true << "#ffff5252";
        QTest::newRow("css-only name in flutter") << "olive" << F << true << "#ff808000";
        QTest::newRow("transparent") << "transparent" << F << true << "#00000000";
        QTest::newRow("rgb") << "rgb(230, 0, 0)" << C << true << "#ffe60000";
        QTest::newRow("rgba") << "rgba(0, 128, 0, 0.5)" << F << true << "#80008000";
        QTest::newRow("rgb percent") << "rgb(100%, 0%, 50%)" << C << true << "#ffff0080";
        QTest::newRow("css4 rgb") << "rgb(0 0 255 / 50%)" << C << true << "#800000ff";
        QTest::newRow("hsl") << "hsl(120, 100%, 25%)" << C << true << "#ff008000";
        QTest::newRow("whitespace") << "  #ffffff " << F << true << "#ffffffff";
        QTest::newRow("inherit") << "inherit" << F << false << "";
        QTest::newRow("empty") << "" << F << false << "";
        QTest::newRow("garbage") << "not-a-color" << F << false << "";
        QTest::newRow("bad hex") << "#12345" << F << false << "";
        QTest::newRow("bad rgb") << "rgb(1,2)" << F << false << "";
    }

    void parse()
    {
        QFETCH(QString, value);
        QFETCH(ColorDialect, dialect);
        QFETCH(bool, valid);
        QFETCH(QString, argb);
        const auto c = colors::parse(value, dialect);
        QCOMPARE(c.has_value(), valid);
        if (valid)
            QCOMPARE(c->name(QColor::HexArgb), argb);
    }

    void formatKeepsEquivalentOriginal()
    {
        QCOMPARE(colors::format(QColor(0xF4, 0x43, 0x36), "red"), QStringLiteral("red"));
        QCOMPARE(colors::format(QColor(255, 0, 0), "rgb(255, 0, 0)", ColorDialect::Css),
                 QStringLiteral("rgb(255, 0, 0)"));
        QCOMPARE(colors::format(QColor(255, 0, 0), "#FFFF0000"), QStringLiteral("#FFFF0000"));
    }

    void formatNewValues()
    {
        // flutter_quill writes '#' + AARRGGBB in upper case.
        QCOMPARE(colors::format(QColor(0x21, 0x96, 0xF3)), QStringLiteral("#FF2196F3"));
        QCOMPARE(colors::format(QColor(255, 0, 0), "red"), QStringLiteral("#FFFF0000"));
        QCOMPARE(colors::format(QColor(255, 0, 0), {}, ColorDialect::Css), QStringLiteral("#ff0000"));
        QCOMPARE(colors::format(QColor(255, 0, 0, 128), {}, ColorDialect::Css),
                 QStringLiteral("rgba(255, 0, 0, 0.502)"));
    }

    void formatParsesBack()
    {
        for (const auto dialect : {ColorDialect::Flutter, ColorDialect::Css}) {
            for (const QColor c : {QColor(1, 2, 3), QColor(200, 100, 50, 77), QColor(0, 0, 0, 0)}) {
                const auto back = colors::parse(colors::format(c, {}, dialect), dialect);
                QVERIFY(back);
                QCOMPARE(back->rgba(), c.rgba());
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestColors)
#include "tst_colors.moc"
