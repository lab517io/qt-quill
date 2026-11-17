// Complexity guard for large documents (milestone M11): times an operation
// on a document and on one four times larger and checks the ratio, which
// does not depend on the machine's speed. Operations that should not grow
// with the document (an edit at one place) must stay below 2.5x; linear ones
// (load, export, undo of a paste) below 8x. A quadratic regression shows as
// about 16x. Absolute timings: tools/bench (quill-bench). Edits in long lists
// are not timed here: Qt numbers list items in linear time, which hides
// quill-qt's part; tst_editor_api::editsKeepUntouchedLists checks that the
// list regrouping leaves unchanged lists alone instead.
#include "Corpora.h"

#include <quill/DeltaDocument.h>
#include <quill/EmbedRegistry.h>
#include <quill/QuillEditor.h>

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QMimeData>
#include <QTest>

#include <algorithm>
#include <functional>

using namespace quill;

namespace {

constexpr double kConstant = 2.5;
constexpr double kLinear = 8.0;

double median(QList<double> v)
{
    std::sort(v.begin(), v.end());
    return v.isEmpty() ? 0 : v.at(v.size() / 2);
}

std::unique_ptr<QuillEditor> editorWith(const Delta& doc)
{
    auto e = std::make_unique<QuillEditor>();
    e->embeds()->setRemoteImagesEnabled(false);
    e->resize(800, 600);
    e->show();
    if (!QTest::qWaitForWindowExposed(e.get()))
        qWarning("not exposed");
    e->setContents(doc);
    e->viewport()->repaint();
    return e;
}

/// Median time (ms) of `op` run `samples` times on an editor holding `doc`;
/// the best of two rounds, against noise.
double timeOn(const Delta& doc, int samples, const std::function<void(QuillEditor*, int)>& op)
{
    double best = 1e18;
    for (int round = 0; round < 2; ++round) {
        auto e = editorWith(doc);
        QList<double> ms;
        for (int i = 0; i < samples; ++i) {
            QElapsedTimer t;
            t.start();
            op(e.get(), i);
            ms << double(t.nsecsElapsed()) / 1e6;
        }
        best = std::min(best, median(ms));
    }
    return best;
}

void checkRatio(const char* what, double small, double large, double limit)
{
    const double ratio = large / std::max(small, 1e-3);
    qInfo("%s: %.3f ms -> %.3f ms (x%.2f, limit x%.1f)", what, small, large, ratio, limit);
    QVERIFY2(ratio <= limit, qPrintable(QStringLiteral("%1 grows x%2 for a 4x larger document (limit x%3)")
                                            .arg(QLatin1String(what))
                                            .arg(ratio, 0, 'f', 2)
                                            .arg(limit, 0, 'f', 1)));
}

void typeOne(QuillEditor* e, int)
{
    QTest::keyClick(e, Qt::Key_A);
    e->viewport()->repaint();
}

void insertMiddle(QuillEditor* e, int)
{
    e->updateContents(Delta().retain(e->getLength() / 2).insert(QStringLiteral("x")));
    e->viewport()->repaint();
}

} // namespace

class TestScaling : public QObject
{
    Q_OBJECT

    // Sizes: a fraction of quill-bench's corpora, small enough for Debug and
    // ASan runs, large enough for the document-dependent part to show.
    static constexpr double kSmall = 0.025;
    static constexpr double kLarge = kSmall * 4;

    Delta corpus(const char* name, double scale) { return quillbench::corpus(QLatin1String(name), scale); }

private Q_SLOTS:
    void typingInLargeDocument()
    {
        const auto at = [](QuillEditor* e, int i) {
            if (i == 0)
                e->setSelection(e->getLength() / 2, 0);
            typeOne(e, i);
        };
        checkRatio("typing (mixed)", timeOn(corpus("mixed", kSmall), 40, at), timeOn(corpus("mixed", kLarge), 40, at),
                   kConstant);
    }

    void apiEditInLargeDocument()
    {
        checkRatio("one-character update (mixed)", timeOn(corpus("mixed", kSmall), 40, insertMiddle),
                   timeOn(corpus("mixed", kLarge), 40, insertMiddle), kConstant);
    }

    void apiEditInLongLine()
    {
        // Qt lays the whole paragraph out again: linear in its length. Longer
        // lines than elsewhere (20 000 and 80 000 characters), so that work
        // per formatted run of the line would show next to Qt's layout.
        checkRatio("one-character update (long line)", timeOn(corpus("long-line", 0.1), 10, insertMiddle),
                   timeOn(corpus("long-line", 0.4), 10, insertMiddle), 6.0);
    }

    void exportPlainText()
    {
        const auto exportAll = [](QuillEditor* e, int) {
            volatile auto n = DeltaDocument::toDelta(e->document()).length();
            Q_UNUSED(n);
        };
        checkRatio("export (plain lines)", timeOn(corpus("plain", kSmall), 3, exportAll),
                   timeOn(corpus("plain", kLarge), 3, exportAll), kLinear);
    }

    void load()
    {
        const Delta small = corpus("mixed", kSmall), large = corpus("mixed", kLarge);
        const auto loadOf = [](const Delta& doc) {
            return [doc](QuillEditor* e, int) {
                e->setContents(doc);
                e->viewport()->repaint();
            };
        };
        checkRatio("load (mixed)", timeOn(small, 3, loadOf(small)), timeOn(large, 3, loadOf(large)), kLinear);
    }

    void undoPaste()
    {
        auto* mime = new QMimeData;
        mime->setHtml(quillbench::pasteHtml(200));
        QApplication::clipboard()->setMimeData(mime);
        const auto pasteUndo = [](QuillEditor* e, int) {
            e->setSelection(e->getLength() / 2, 0);
            e->paste();
            QTest::keyClick(e, Qt::Key_Z, Qt::ControlModifier);
            e->viewport()->repaint();
        };
        checkRatio("paste and undo (mixed)", timeOn(corpus("mixed", kSmall), 3, pasteUndo),
                   timeOn(corpus("mixed", kLarge), 3, pasteUndo), kLinear);
    }
};

QTEST_MAIN(TestScaling)
#include "tst_scaling.moc"
