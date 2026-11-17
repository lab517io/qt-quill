// quill-bench: timings of quill-qt on large documents (milestone M11).
//
//   quill-bench [--corpus a,b] [--scenario x,y] [--scale 1.0] [--json out.json]
//
// Every scenario starts from a freshly loaded document in a shown
// QuillEditor (offscreen platform recommended) and records one sample per
// operation; the table shows median, 95th percentile and maximum in ms.
// Build in Release; numbers from Debug or ASan builds are meaningless.
#include "Corpora.h"

#include <quill/DeltaDocument.h>
#include <quill/EmbedRegistry.h>
#include <quill/QuillEditor.h>

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QScrollBar>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>

#include <algorithm>
#include <functional>

using namespace quill;

namespace {

struct Result
{
    QString corpus, scenario;
    QList<double> ms;
    QString note;

    double percentile(double p) const
    {
        if (ms.isEmpty())
            return 0;
        QList<double> s = ms;
        std::sort(s.begin(), s.end());
        return s.at(std::min<qsizetype>(s.size() - 1, qsizetype(p * double(s.size() - 1) + 0.5)));
    }
};

double rssMiB()
{
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray& line : f.readAll().split('\n')) {
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').first().toDouble() / 1024.0;
    }
    return 0;
}

template<typename F>
double timeMs(F&& f)
{
    QElapsedTimer t;
    t.start();
    f();
    return double(t.nsecsElapsed()) / 1e6;
}

void expose(QWidget* w)
{
    if (!QTest::qWaitForWindowExposed(w))
        qWarning("window not exposed");
}

/// Lays out and paints what is visible, as a frame would.
void frame(QWidget* w)
{
    w->repaint();
}

class Bench
{
public:
    Bench(const QString& fixtures) : m_fixtures(fixtures) {}

    QList<Result> run(const QString& corpusName, double scale, const QStringList& scenarios)
    {
        QList<Result> out;
        QElapsedTimer gen;
        gen.start();
        const Delta doc = quillbench::corpus(corpusName, scale, m_fixtures);
        qInfo("%s: %lld ops, length %lld (generated in %lld ms)", qPrintable(corpusName),
              static_cast<long long>(doc.ops().size()), static_cast<long long>(doc.length()),
              static_cast<long long>(gen.elapsed()));
        for (const QString& s : scenarios) {
            Result r;
            r.corpus = corpusName;
            r.scenario = s;
            QElapsedTimer total;
            total.start();
            runScenario(s, doc, r);
            qInfo("  %-16s median %8.2f  p95 %8.2f  max %8.2f ms  (%lld samples, %.1f s)%s", qPrintable(s),
                  r.percentile(0.5), r.percentile(0.95), r.percentile(1.0), static_cast<long long>(r.ms.size()),
                  double(total.elapsed()) / 1000.0, r.note.isEmpty() ? "" : qPrintable(QStringLiteral("  ") + r.note));
            out << r;
        }
        return out;
    }

private:
    std::unique_ptr<QuillEditor> editor(const Delta& doc)
    {
        auto e = std::make_unique<QuillEditor>();
        e->embeds()->setRemoteImagesEnabled(false);
        e->resize(900, 700);
        e->show();
        expose(e.get());
        e->setContents(doc);
        frame(e->viewport());
        return e;
    }

    static void typeAt(QuillEditor* e, int index, int keys, Result& r)
    {
        e->setSelection(index, 0);
        for (int i = 0; i < keys; ++i)
            r.ms << timeMs([&] {
                QTest::keyClick(e, Qt::Key_A);
                frame(e->viewport());
            });
    }

    void runScenario(const QString& s, const Delta& doc, Result& r)
    {
        if (s == QLatin1String("load")) {
            const double before = rssMiB();
            for (int i = 0; i < 3; ++i) {
                auto e = std::make_unique<QuillEditor>();
                e->embeds()->setRemoteImagesEnabled(false);
                e->resize(900, 700);
                e->show();
                expose(e.get());
                r.ms << timeMs([&] {
                    e->setContents(doc);
                    frame(e->viewport());
                });
                if (i == 0)
                    r.note = QStringLiteral("RSS +%1 MiB").arg(rssMiB() - before, 0, 'f', 0);
            }
            return;
        }
        if (s == QLatin1String("compose")) { // the Delta part of every change
            const Delta change = Delta().retain(doc.length() / 2).insert(QStringLiteral("x"));
            for (int i = 0; i < 50; ++i)
                r.ms << timeMs([&] {
                    volatile auto n = doc.compose(change).ops().size();
                    Q_UNUSED(n);
                });
            return;
        }
        if (s == QLatin1String("qt-load")) { // baseline: Qt alone
            for (int i = 0; i < 3; ++i) {
                QTextEdit edit;
                edit.resize(900, 700);
                edit.show();
                expose(&edit);
                r.ms << timeMs([&] {
                    DeltaDocument::load(edit.document(), doc);
                    frame(edit.viewport());
                });
            }
            return;
        }
        if (s == QLatin1String("qt-typing") || s == QLatin1String("qt-type-start")) { // baseline: a plain QTextEdit
            QTextEdit edit;
            edit.resize(900, 700);
            edit.show();
            expose(&edit);
            DeltaDocument::load(edit.document(), doc);
            QTextCursor c(edit.document());
            c.setPosition(s == QLatin1String("qt-typing") ? edit.document()->characterCount() / 2 : 0);
            edit.setTextCursor(c);
            frame(edit.viewport());
            for (int i = 0; i < 200; ++i)
                r.ms << timeMs([&] {
                    QTest::keyClick(&edit, Qt::Key_A);
                    frame(edit.viewport());
                });
            return;
        }
        auto e = editor(doc);
        const int len = e->getLength();
        if (s == QLatin1String("getContents")) {
            for (int i = 0; i < 50; ++i)
                r.ms << timeMs([&] {
                    volatile auto n = e->getContents().ops().size();
                    Q_UNUSED(n);
                });
        } else if (s == QLatin1String("export")) {
            for (int i = 0; i < 5; ++i)
                r.ms << timeMs([&] {
                    volatile auto n = DeltaDocument::toDelta(e->document()).length();
                    Q_UNUSED(n);
                });
        } else if (s == QLatin1String("type-start")) {
            typeAt(e.get(), 0, 200, r);
        } else if (s == QLatin1String("type-middle")) {
            typeAt(e.get(), len / 2, 200, r);
        } else if (s == QLatin1String("type-end")) {
            typeAt(e.get(), len - 1, 200, r);
        } else if (s == QLatin1String("enter-backspace")) {
            e->setSelection(len / 2, 0);
            for (int i = 0; i < 50; ++i) {
                r.ms << timeMs([&] {
                    QTest::keyClick(e.get(), Qt::Key_Return);
                    frame(e->viewport());
                });
                r.ms << timeMs([&] {
                    QTest::keyClick(e.get(), Qt::Key_Backspace);
                    frame(e->viewport());
                });
            }
        } else if (s == QLatin1String("format-line")) {
            for (int i = 0; i < 50; ++i)
                r.ms << timeMs([&] {
                    e->formatLine(len / 2, 1, {{QStringLiteral("header"), i % 2 ? QJsonValue() : QJsonValue(2)}});
                    frame(e->viewport());
                });
        } else if (s == QLatin1String("bold-all")) {
            for (int i = 0; i < 2; ++i)
                r.ms << timeMs([&] {
                    e->formatText(0, len, {{QStringLiteral("bold"), i % 2 ? QJsonValue() : QJsonValue(true)}});
                    frame(e->viewport());
                });
        } else if (s == QLatin1String("update-1char")) {
            for (int i = 0; i < 200; ++i)
                r.ms << timeMs([&] {
                    e->updateContents(Delta().retain(len / 2).insert(QStringLiteral("x")));
                    frame(e->viewport());
                });
        } else if (s == QLatin1String("paste-undo")) {
            auto* mime = new QMimeData;
            mime->setHtml(quillbench::pasteHtml());
            QApplication::clipboard()->setMimeData(mime);
            e->setSelection(len / 2, 0);
            r.ms << timeMs([&] {
                e->paste();
                frame(e->viewport());
            });
            r.ms << timeMs([&] {
                QTest::keyClick(e.get(), Qt::Key_Z, Qt::ControlModifier);
                frame(e->viewport());
            });
            r.ms << timeMs([&] {
                QTest::keyClick(e.get(), Qt::Key_Y, Qt::ControlModifier);
                frame(e->viewport());
            });
            r.note = QStringLiteral("samples: paste, undo, redo = %1 / %2 / %3 ms")
                         .arg(r.ms[0], 0, 'f', 1)
                         .arg(r.ms[1], 0, 'f', 1)
                         .arg(r.ms[2], 0, 'f', 1);
        } else if (s == QLatin1String("find")) {
            int matches = 0;
            for (int i = 0; i < 3; ++i)
                r.ms << timeMs([&] {
                    QTextCursor c(e->document());
                    matches = 0;
                    while (!(c = e->document()->find(QStringLiteral("lorem"), c)).isNull())
                        ++matches;
                });
            r.note = QStringLiteral("%1 matches").arg(matches);
        } else if (s == QLatin1String("scroll")) {
            QScrollBar* bar = e->verticalScrollBar();
            const int steps = 100;
            for (int i = 1; i <= steps; ++i)
                r.ms << timeMs([&] {
                    bar->setValue(bar->maximum() * i / steps);
                    frame(e->viewport());
                });
        } else {
            qFatal("unknown scenario %s", qPrintable(s));
        }
    }

    QString m_fixtures;
};

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QStringList allScenarios{
        QStringLiteral("load"),          QStringLiteral("qt-load"),         QStringLiteral("compose"),
        QStringLiteral("getContents"),   QStringLiteral("export"),          QStringLiteral("type-start"),
        QStringLiteral("type-middle"),   QStringLiteral("type-end"),        QStringLiteral("qt-typing"),
        QStringLiteral("qt-type-start"), QStringLiteral("enter-backspace"), QStringLiteral("format-line"),
        QStringLiteral("update-1char"),  QStringLiteral("paste-undo"),      QStringLiteral("bold-all"),
        QStringLiteral("find"),          QStringLiteral("scroll")};
    QCommandLineParser p;
    p.addHelpOption();
    p.addOption({QStringLiteral("corpus"),
                 QStringLiteral("Corpora (comma separated): ") + quillbench::corpusNames().join(QLatin1Char(',')),
                 QStringLiteral("names"), quillbench::corpusNames().join(QLatin1Char(','))});
    p.addOption({QStringLiteral("scenario"),
                 QStringLiteral("Scenarios (comma separated): ") + allScenarios.join(QLatin1Char(',')),
                 QStringLiteral("names"), allScenarios.join(QLatin1Char(','))});
    p.addOption(
        {QStringLiteral("scale"), QStringLiteral("Corpus size factor."), QStringLiteral("x"), QStringLiteral("1")});
    p.addOption({QStringLiteral("json"), QStringLiteral("Write the samples as JSON."), QStringLiteral("file")});
    p.addOption({QStringLiteral("fixtures"), QStringLiteral("tests/fixtures directory."), QStringLiteral("dir"),
                 QStringLiteral(QUILLQT_FIXTURES_DIR)});
    p.process(app);

    Bench bench(p.value(QStringLiteral("fixtures")));
    QJsonArray json;
    const QStringList scenarios = p.value(QStringLiteral("scenario")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString& c : p.value(QStringLiteral("corpus")).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        for (const Result& r : bench.run(c, p.value(QStringLiteral("scale")).toDouble(), scenarios)) {
            QJsonArray samples;
            for (double v : r.ms)
                samples.append(v);
            json.append(QJsonObject{{QStringLiteral("corpus"), r.corpus},
                                    {QStringLiteral("scenario"), r.scenario},
                                    {QStringLiteral("median"), r.percentile(0.5)},
                                    {QStringLiteral("p95"), r.percentile(0.95)},
                                    {QStringLiteral("max"), r.percentile(1.0)},
                                    {QStringLiteral("note"), r.note},
                                    {QStringLiteral("samples"), samples}});
        }
    }
    if (p.isSet(QStringLiteral("json"))) {
        QFile f(p.value(QStringLiteral("json")));
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(json).toJson());
    }
    return 0;
}
