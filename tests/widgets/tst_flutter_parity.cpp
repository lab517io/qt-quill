// Keyboard and toolbar parity with flutter_quill (milestone M8): replays the
// editing scripts recorded in a real flutter_quill editor
// (tests/fixtures/flutter_quill/scripts.json, written by
// tools/flutter-check/test/export_test.dart) and compares the documents.
#include "EditorTestUtil.h"

#include <quill/EmbedRegistry.h>

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

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
struct LineBox
{
    double top = 0, height = 0;
};

/// Content box of every line (Quill line = QTextBlock), as flutter_quill's
/// geometry probe records it: from the first to the last text line of the
/// block, without block margins.
QList<LineBox> lineBoxes(QTextDocument* doc)
{
    doc->documentLayout()->documentSize(); // lay out
    QList<LineBox> out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const QTextLayout* l = b.layout();
        if (l->lineCount() == 0) {
            out.append({l->position().y(), 0});
            continue;
        }
        const QTextLine first = l->lineAt(0);
        const QTextLine last = l->lineAt(l->lineCount() - 1);
        const double top = l->position().y() + first.y();
        out.append({top, l->position().y() + last.y() + last.height() - top});
    }
    return out;
}

/// The style the geometry is compared at: a 16 px base font, so that
/// flutter_quill's logical pixels are quill-qt's pixels.
StyleOptions geometryStyle()
{
    StyleOptions style;
    style.baseFont = QFont(QStringLiteral("DejaVu Sans"));
    style.baseFont.setPixelSize(16);
    return style;
}

} // namespace

class TestFlutterParity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Gaps between consecutive lines (spacing and padding only; line heights
    // depend on fonts) must match flutter_quill's, recorded by
    // tools/flutter-check/test/geometry_test.dart; after a line holding only
    // a sized embed, the distance between the line tops must match.
    void geometry_data()
    {
        QTest::addColumn<QString>("file");
        const QDir dir(testutil::fixturePath(QStringLiteral("flutter_geometry")));
        for (const QString& f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
            if (f != QLatin1String("known_differences.json"))
                QTest::newRow(qPrintable(f)) << dir.filePath(f);
        }
    }

    void geometry()
    {
        QFETCH(QString, file);
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject probe = QJsonDocument::fromJson(f.readAll()).object();
        const QString source = probe.value(QStringLiteral("source")).toString();
        const QJsonArray expected = probe.value(QStringLiteral("lines")).toArray();
        const QJsonObject known = testutil::loadJson(QStringLiteral("flutter_geometry/known_differences.json"))
                                      .object()
                                      .value(source)
                                      .toObject();

        const std::optional<Delta> delta = Delta::fromJson(testutil::loadJson(source).toJson());
        QVERIFY(delta);
        QTextDocument doc;
        LoadOptions options;
        options.style = geometryStyle();
        doc.setDefaultFont(options.style.baseFont);
        const LoadResult r = DeltaDocument::load(&doc, *delta, options);
        QVERIFY(r.ok);
        doc.setDocumentMargin(0);
        doc.setTextWidth(probe.value(QStringLiteral("width")).toDouble());
        const QList<LineBox> lines = lineBoxes(&doc);
        QCOMPARE(lines.size(), expected.size());

        QStringList problems;
        QSet<QString> seen;
        const QStringList texts = [&] {
            QStringList t;
            for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
                t << b.text().replace(QChar::ObjectReplacementCharacter, QStringLiteral("[E]")).left(28);
            return t;
        }();
        QList<AttributeMap> lineAttrs;
        for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
            lineAttrs << b.blockFormat().property(prop::BlockAttributes).toJsonObject();
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QJsonObject e0 = expected[i - 1].toObject(), e1 = expected[i].toObject();
            const QString embed = e0[QStringLiteral("embed")].toString();
            if (embed == QLatin1String("unsized"))
                continue; // its height is the image's natural size
            // Without fonts (Qt's offscreen platform on Windows) there is no
            // text descent below a lone image.
            if (embed == QLatin1String("sized") && QFontDatabase::families().isEmpty())
                continue;
            // After a sized embed, compare the distance between line tops
            // (the line is the embed's height plus the text descent).
            // Likewise after a line with a line height: flutter_quill spreads
            // the extra height around the text, Qt puts it below.
            const bool pitch =
                embed == QLatin1String("sized") || lineAttrs[i - 1].contains(QLatin1String("line-height"));
            const double want = e1[QStringLiteral("top")].toDouble() - e0[QStringLiteral("top")].toDouble()
                                - (pitch ? 0 : e0[QStringLiteral("height")].toDouble());
            const double got = lines[i].top - lines[i - 1].top - (pitch ? 0 : lines[i - 1].height);
            QString problem;
            // A line height scales Qt's natural line height, which differs a
            // little from the font size flutter_quill scales: allow 4 %.
            const double tolerance = pitch ? std::max(1.0, 0.04 * want) : 1.0;
            if (std::abs(got - want) > tolerance)
                problem = QStringLiteral("%1 %2, flutter_quill %3")
                              .arg(pitch ? QStringLiteral("distance from the line above") : QStringLiteral("gap"))
                              .arg(got, 0, 'f', 1)
                              .arg(want, 0, 'f', 1);
            const QString key = QString::number(i);
            if (known.contains(key)) {
                seen.insert(key);
                if (problem.isEmpty())
                    problems << QStringLiteral("line %1 (%2) is listed in known_differences.json but matches")
                                    .arg(i)
                                    .arg(texts[i]);
            } else if (!problem.isEmpty()) {
                problems << QStringLiteral("line %1 (%2): %3").arg(i).arg(texts[i], problem.trimmed());
            }
        }
        if (!problems.isEmpty())
            QFAIL(qPrintable(source + QStringLiteral(":\n  ") + problems.join(QStringLiteral("\n  "))));
    }

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

    // Images resized in quill-qt (M13); flutter_quill must render them at the
    // written size (geometry_test.dart probes this export).
    void imageResizeExport()
    {
        auto e = testutil::makeEditor(true);
        const auto a = [](const char* json) {
            return QJsonDocument::fromJson(QByteArray(json).replace('\'', '"')).object();
        };
        const QString owl = QStringLiteral("https://flutter.github.io/assets-for-api-docs/assets/widgets/owl.jpg");
        e->setContents(Delta()
                           .insert(QStringLiteral("Resized images\n"))
                           .insert(QJsonObject{{QStringLiteral("image"), owl}})
                           .insert(QStringLiteral("\n"))
                           .insert(QJsonObject{{QStringLiteral("image"), owl}},
                                   a("{'style':'margin: 4px; mobileAlignment: center'}"))
                           .insert(QStringLiteral("\n"), a("{'align':'center'}"))
                           .insert(QJsonObject{{QStringLiteral("image"), owl}}, a("{'width':'90'}"))
                           .insert(QStringLiteral("\nafter\n")));
        // Natural size of an unloaded image is the placeholder; give each a size.
        e->resizeEmbed(15, QSizeF(240.3, 160.4), Source::User);
        e->resizeEmbed(17, QSizeF(320, 180), Source::User);
        e->resizeEmbed(19, QSizeF(120, 80), Source::User); // Quill.js width/height
        QCOMPARE(e->verificationFailures(), 0);
        recordExport(QStringLiteral("image_resize"), e->getContents());
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
                case 3: {
                    // One rnd.next() per statement: argument evaluation order
                    // is unspecified and differs between compilers.
                    const int end = boundary(index + 1 + rnd.next(6));
                    const QString name = inlineKeys.at(rnd.next(int(inlineKeys.size())));
                    const QJsonValue value = rnd.next(4) ? QJsonValue(true) : QJsonValue();
                    e->formatText(index, end - index, {{name, value}}, Source::User);
                } break;
                case 4: {
                    const int length = rnd.next(4);
                    e->formatLine(index, length, lineFormats.at(rnd.next(int(lineFormats.size()))), Source::User);
                } break;
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
