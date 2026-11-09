// Keyboard and toolbar parity with flutter_quill (milestone M8): replays the
// editing scripts recorded in a real flutter_quill editor
// (tests/fixtures/flutter_quill/scripts.json, written by
// tools/flutter-check/test/export_test.dart) and compares the documents.
#include "EditorTestUtil.h"

#include <quill/EmbedRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace quill;

namespace {

QJsonObject scriptsFile()
{
    QFile f(testutil::fixturePath(QStringLiteral("flutter_quill/scripts.json")));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("scripts.json missing");
    return QJsonDocument::fromJson(f.readAll()).object();
}

void runStep(QuillEditor* e, const QJsonObject& step)
{
    if (step.contains(QStringLiteral("type"))) {
        QTest::keyClicks(e, step.value(QStringLiteral("type")).toString());
    } else if (step.contains(QStringLiteral("select"))) {
        const QJsonArray s = step.value(QStringLiteral("select")).toArray();
        e->setSelection(s.at(0).toInt(), s.at(1).toInt(), Source::User);
    } else if (step.contains(QStringLiteral("format"))) {
        const QJsonObject f = step.value(QStringLiteral("format")).toObject();
        for (auto it = f.begin(); it != f.end(); ++it)
            e->format(it.key(), it.value(), Source::User);
    } else {
        const QString key = step.value(QStringLiteral("key")).toString();
        if (key == QLatin1String("enter"))
            QTest::keyClick(e, Qt::Key_Return);
        else if (key == QLatin1String("backspace"))
            QTest::keyClick(e, Qt::Key_Backspace);
        else if (key == QLatin1String("tab"))
            QTest::keyClick(e, Qt::Key_Tab);
        else if (key == QLatin1String("shift+tab"))
            QTest::keyClick(e, Qt::Key_Backtab, Qt::ShiftModifier);
        else
            qFatal("unknown key %s", qPrintable(key));
    }
}

QString compact(const QJsonValue& v)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
}

/// Deterministic on every platform (std distributions are not).
struct Lcg
{
    quint32 state;
    int next(int n)
    {
        state = state * 1664525u + 1013904223u;
        return int((state >> 8) % quint32(n));
    }
};

/// Compares `doc` with the committed export, or writes it when
/// QUILLQT_WRITE_FIXTURES=1. The exports are what flutter_quill must load
/// (tools/flutter-check/test/roundtrip_test.dart).
void recordExport(const QString& name, const Delta& doc)
{
    const QString path = testutil::fixturePath(QStringLiteral("quillqt_exports/") + name + QStringLiteral(".json"));
    if (qEnvironmentVariableIntValue("QUILLQT_WRITE_FIXTURES") == 1) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(doc.toJsonArray()).toJson(QJsonDocument::Indented));
        return;
    }
    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly),
             qPrintable(path + QStringLiteral(" missing; run with QUILLQT_WRITE_FIXTURES=1")));
    const auto committed = Delta::fromJson(f.readAll());
    QVERIFY(committed);
    QCOMPARE(compact(doc.toJsonArray()), compact(committed->toJsonArray()));
}

} // namespace

class TestFlutterParity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scripts_data()
    {
        QTest::addColumn<QJsonObject>("script");
        for (const QJsonValue& v : scriptsFile().value(QStringLiteral("scripts")).toArray())
            QTest::newRow(qPrintable(v[QStringLiteral("name")].toString())) << v.toObject();
    }

    void scripts()
    {
        QFETCH(QJsonObject, script);
        auto e = testutil::makeEditor(true);
        testutil::ChangeRecorder rec(e.get());
        e->setFocus();
        for (const QJsonValue& step : script.value(QStringLiteral("steps")).toArray())
            runStep(e.get(), step.toObject());
        const auto expected = Delta::fromJson(script.value(QStringLiteral("result")));
        QVERIFY(expected);
        QCOMPARE(compact(e->getContents().toJsonArray()), compact(expected->toJsonArray()));
        const QJsonArray sel = script.value(QStringLiteral("selection")).toArray();
        QCOMPARE(e->getSelection(), (Range{sel.at(0).toInt(), sel.at(1).toInt()}));
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    // Documents written by quill-qt for flutter_quill to load.
    void exportToolbarDocument()
    {
        auto e = testutil::makeEditor(true);
        e->setFocus();
        const auto line = [&](const QString& text, const AttributeMap& inlineAttrs, const AttributeMap& lineAttrs) {
            const int start = e->getSelection().index;
            bool ascii = true;
            for (const QChar ch : text)
                ascii = ascii && ch.unicode() < 128;
            if (ascii)
                QTest::keyClicks(e.get(), text);
            else // QTest can only type ASCII
                e->insertText(start, text, {}, Source::User);
            e->setSelection(start, int(text.size()));
            for (auto it = inlineAttrs.begin(); it != inlineAttrs.end(); ++it)
                e->format(it.key(), it.value(), Source::User);
            for (auto it = lineAttrs.begin(); it != lineAttrs.end(); ++it)
                e->format(it.key(), it.value(), Source::User);
            e->setSelection(start + int(text.size()));
            QTest::keyClick(e.get(), Qt::Key_Return);
        };
        const auto a = [](const char* json) {
            return QJsonDocument::fromJson(QByteArray(json).replace('\'', '"')).object();
        };
        line(QStringLiteral("Bold"), a("{'bold':true}"), {});
        line(QStringLiteral("Italic underline strike"), a("{'italic':true,'underline':true,'strike':true}"), {});
        line(QStringLiteral("inline code"), a("{'code':true}"), {});
        line(QStringLiteral("sub"), a("{'script':'sub'}"), {});
        line(QStringLiteral("super"), a("{'script':'super'}"), {});
        line(QStringLiteral("Red on yellow"), a("{'color':'#FFF44336','background':'#FFFFEB3B'}"), {});
        line(QStringLiteral("Monospace large"), a("{'font':'monospace','size':'large'}"), {});
        line(QStringLiteral("Size 20"), a("{'size':'20'}"), {});
        line(QStringLiteral("Link"), a("{'link':'https://pub.dev/packages/flutter_quill'}"), {});
        for (int level = 1; level <= 3; ++level)
            line(QStringLiteral("Heading %1").arg(level), {}, AttributeMap{{QStringLiteral("header"), level}});
        line(QStringLiteral("Bullet"), {}, a("{'list':'bullet'}"));
        line(QStringLiteral("Nested"), {}, a("{'list':'bullet','indent':1}"));
        line(QStringLiteral("Ordered"), {}, a("{'list':'ordered','indent':null}"));
        line(QStringLiteral("Checked"), {}, a("{'list':'checked'}"));
        line(QStringLiteral("Unchecked"), {}, a("{'list':'unchecked'}"));
        line(QStringLiteral("Quote"), {}, a("{'list':null,'blockquote':true}"));
        line(QStringLiteral("code();"), {}, a("{'code-block':true}"));
        line(QStringLiteral("Centered"), {}, a("{'code-block':null,'align':'center'}"));
        line(QStringLiteral("مرحبا"), {}, a("{'align':'right','direction':'rtl'}"));
        e->format(QStringLiteral("align"), QJsonValue::Null, Source::User);
        e->format(QStringLiteral("direction"), QJsonValue::Null, Source::User);
        const int at = e->getSelection().index;
        e->insertBlockEmbed(at, QStringLiteral("image"),
                            QStringLiteral("https://flutter.github.io/assets-for-api-docs/assets/widgets/owl.jpg"),
                            Source::User);
        e->formatText(at, 1, a("{'width':'200','height':'150'}"), Source::User);
        e->insertBlockEmbed(e->getLength() - 1, QStringLiteral("video"),
                            QStringLiteral("https://www.youtube.com/watch?v=V4hgdKhIqtc"), Source::User);
        e->insertText(e->getLength() - 1, QStringLiteral("\nFormula "), {}, Source::User);
        e->insertEmbed(e->getLength() - 1, QStringLiteral("formula"), QStringLiteral("e^{i\\pi}+1=0"), Source::User);
        e->insertText(e->getLength() - 1, QStringLiteral("\n"), {}, Source::User);
        e->insertBlockEmbed(
            e->getLength() - 1, QStringLiteral("custom"),
            CustomEmbedRenderer::encode(QStringLiteral("notes"),
                                        QString::fromUtf8(Delta().insert(QStringLiteral("Note ü 🎉\n")).toJson())),
            Source::User);
        QCOMPARE(e->verificationFailures(), 0);
        recordExport(QStringLiteral("toolbar"), e->getContents());
    }

    void exportKeyboardDocument()
    {
        auto e = testutil::makeEditor(true);
        e->setFocus();
        // List shortcuts, continuation, Tab indent, exits, typing formats.
        QTest::keyClicks(e.get(), QStringLiteral("1. one"));
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Tab);
        QTest::keyClicks(e.get(), QStringLiteral("nested"));
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Tab, Qt::ShiftModifier);
        QTest::keyClicks(e.get(), QStringLiteral("two"));
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClicks(e.get(), QStringLiteral("- bullet"));
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClicks(e.get(), QStringLiteral("[] task"));
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_Backspace);
        QTest::keyClick(e.get(), Qt::Key_Return);
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClicks(e.get(), QStringLiteral("bold"));
        QTest::keyClick(e.get(), Qt::Key_B, Qt::ControlModifier);
        QTest::keyClicks(e.get(), QStringLiteral(" plain\ttab"));
        QCOMPARE(e->verificationFailures(), 0);
        recordExport(QStringLiteral("keyboard"), e->getContents());
    }

    void exportRandomEdits()
    {
        const QStringList inlineKeys{QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("underline"),
                                     QStringLiteral("strike"), QStringLiteral("code")};
        const QList<AttributeMap> lineFormats{{{QStringLiteral("header"), 1}},
                                              {{QStringLiteral("header"), 2}},
                                              {{QStringLiteral("list"), QStringLiteral("bullet")}},
                                              {{QStringLiteral("list"), QStringLiteral("ordered")}},
                                              {{QStringLiteral("list"), QStringLiteral("checked")}},
                                              {{QStringLiteral("blockquote"), true}},
                                              {{QStringLiteral("code-block"), true}},
                                              {{QStringLiteral("align"), QStringLiteral("center")}},
                                              {{QStringLiteral("indent"), QStringLiteral("+1")}},
                                              {{QStringLiteral("header"), QJsonValue::Null}},
                                              {{QStringLiteral("list"), QJsonValue::Null}}};
        const QStringList words{QStringLiteral("alpha"), QStringLiteral(" beta "), QStringLiteral("γ"),
                                QStringLiteral("\n"),    QStringLiteral("😀"),     QStringLiteral("x\ny")};
        for (quint32 seed = 1; seed <= 4; ++seed) {
            auto e = testutil::makeEditor(false);
            Lcg rnd{seed};
            // Indices never split a surrogate pair: the API would allow it
            // (as Quill.js does), but flutter_quill cannot lay out lone
            // surrogates.
            const auto boundary = [&](int i) {
                const QString text = e->getText();
                return i > 0 && i < text.size() && text.at(i).isLowSurrogate() ? i + 1 : i;
            };
            for (int step = 0; step < 60; ++step) {
                const int len = e->getLength();
                const int index = std::min(boundary(rnd.next(len)), len - 1);
                switch (rnd.next(6)) {
                case 0:
                case 1:
                    e->insertText(index, words.at(rnd.next(int(words.size()))), {}, Source::User);
                    break;
                case 2:
                    e->deleteText(index, boundary(index + 1 + rnd.next(3)) - index, Source::User);
                    break;
                case 3:
                    e->formatText(index, boundary(index + 1 + rnd.next(6)) - index,
                                  {{inlineKeys.at(rnd.next(int(inlineKeys.size()))),
                                    rnd.next(4) ? QJsonValue(true) : QJsonValue()}},
                                  Source::User);
                    break;
                case 4:
                    e->formatLine(index, rnd.next(4), lineFormats.at(rnd.next(int(lineFormats.size()))), Source::User);
                    break;
                case 5:
                    if (rnd.next(3) == 0)
                        e->insertBlockEmbed(index, QStringLiteral("image"), QStringLiteral("https://example.com/i.png"),
                                            Source::User);
                    else
                        e->insertText(index, QStringLiteral("text"), {}, Source::User);
                    break;
                }
            }
            QCOMPARE(e->verificationFailures(), 0);
            recordExport(QStringLiteral("random_%1").arg(seed), e->getContents());
        }
    }
};

QTEST_MAIN(TestFlutterParity)
#include "tst_flutter_parity.moc"
