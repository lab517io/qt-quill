// Pasting clipboard HTML and text from other applications (milestone M9a).
// Each tests/fixtures/paste/<name>.html is pasted into an empty editor and
// must give <name>.expected.json; QUILLQT_WRITE_FIXTURES=1 writes the
// expected files (review them before committing).
#include "EditorTestUtil.h"

#include <quill/EmbedRegistry.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QTest>

using namespace quill;

namespace {

QString pasteDir()
{
    return testutil::fixturePath(QStringLiteral("paste"));
}

QString compact(const Delta& d)
{
    return QString::fromUtf8(d.toJson());
}

/// An editor whose insertFromMimeData (what Ctrl+V and drops call) is public.
class PasteEditor : public QuillEditor
{
public:
    PasteEditor()
    {
        setVerifyChanges(true);
        embeds()->setRemoteImagesEnabled(false);
    }
    using QuillEditor::insertFromMimeData;
};

Delta paste(PasteEditor* e, const QMimeData& mime)
{
    e->setContents(Delta().insert(QStringLiteral("\n")));
    e->setSelection(0, 0);
    e->insertFromMimeData(&mime);
    return e->getContents();
}

} // namespace

class TestPaste : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void html_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString& f : QDir(pasteDir()).entryList({QStringLiteral("*.html")}, QDir::Files, QDir::Name))
            QTest::newRow(qPrintable(f)) << f.chopped(5);
    }

    void html()
    {
        QFETCH(QString, name);
        QFile in(pasteDir() + QLatin1Char('/') + name + QStringLiteral(".html"));
        QVERIFY(in.open(QIODevice::ReadOnly));
        QMimeData mime;
        mime.setHtml(QString::fromUtf8(in.readAll()));
        auto e = std::make_unique<PasteEditor>();
        testutil::ChangeRecorder rec(e.get());
        const Delta got = paste(e.get(), mime);
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);

        const QString expectedPath = pasteDir() + QLatin1Char('/') + name + QStringLiteral(".expected.json");
        if (qEnvironmentVariableIntValue("QUILLQT_WRITE_FIXTURES") == 1) {
            QFile out(expectedPath);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(got.toJsonArray()).toJson(QJsonDocument::Indented));
            return;
        }
        QFile f(expectedPath);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(expectedPath + QStringLiteral(" missing")));
        const auto expected = Delta::fromJson(f.readAll());
        QVERIFY(expected);
        QCOMPARE(compact(got), compact(*expected));
    }

    void plainTextLineEndings()
    {
        auto e = std::make_unique<PasteEditor>();
        for (const char* text : {"one\r\ntwo\r\n\r\nthree", "one\rtwo\r\rthree", "one\ntwo\n\nthree"}) {
            QMimeData mime;
            mime.setText(QString::fromLatin1(text));
            QCOMPARE(compact(paste(e.get(), mime)), compact(Delta().insert(QStringLiteral("one\ntwo\n\nthree\n"))));
        }
        // Windows file paths stay text.
        QMimeData mime;
        mime.setText(QStringLiteral("C:\\Users\\me\\notes.txt"));
        QCOMPARE(compact(paste(e.get(), mime)), compact(Delta().insert(QStringLiteral("C:\\Users\\me\\notes.txt\n"))));
    }
};

QTEST_MAIN(TestPaste)
#include "tst_paste.moc"
