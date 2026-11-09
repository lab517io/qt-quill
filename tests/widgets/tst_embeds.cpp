// Embed rendering, registry and remote images (milestone M7).
#include "EditorTestUtil.h"

#include <quill/EmbedRegistry.h>
#include <quill/Embeds.h>

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QDir>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

using namespace quill;
using testutil::delta;

namespace {

Delta delta(const QString& json)
{
    return testutil::delta(json.toUtf8().constData());
}

Delta fixture(const QString& name)
{
    const QJsonDocument json = testutil::loadJson(name);
    const auto d = Delta::fromJson(json.isArray() ? QJsonValue(json.array()) : QJsonValue(json.object()));
    if (!d)
        qFatal("bad fixture %s", qPrintable(name));
    return *d;
}

Delta canonical(const Delta& d)
{
    Delta out;
    for (const Op& op : d.ops())
        out.push(op);
    return out;
}

QStringList embedFixtures()
{
    QStringList names;
    const QDir dir(testutil::fixturePath(QStringLiteral("documents")));
    for (const QString& f : dir.entryList({QStringLiteral("embeds*.json")}, QDir::Files, QDir::Name))
        names << QStringLiteral("documents/") + f;
    names << QStringLiteral("flutter_quill/example_default_sample.json");
    return names;
}

/// Positions of all embeds of a document Delta.
QList<int> embedPositions(const Delta& d)
{
    QList<int> out;
    int pos = 0;
    for (const Op& op : d.ops()) {
        if (op.isEmbed())
            out << pos;
        pos += int(op.length());
    }
    return out;
}

/// Rectangle of the character at `pos` in document coordinates.
QRectF charRect(QTextDocument* doc, int pos)
{
    const QTextBlock b = doc->findBlock(pos);
    const int rel = pos - b.position();
    const QRectF br = doc->documentLayout()->blockBoundingRect(b); // lays the block out
    const QTextLine line = b.layout()->lineForTextPosition(rel);
    if (!line.isValid())
        return {};
    const qreal x1 = line.cursorToX(rel), x2 = line.cursorToX(rel + 1);
    return QRectF(br.left() + std::min(x1, x2), br.top() + line.y(), std::abs(x2 - x1), line.height());
}

QImage render(QTextDocument* doc)
{
    const QSizeF size = doc->size();
    QImage image(size.toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    doc->drawContents(&p);
    return image;
}

int inkIn(const QImage& image, const QRect& r)
{
    int ink = 0;
    const QRect clipped = r.intersected(image.rect());
    for (int y = clipped.top(); y <= clipped.bottom(); ++y)
        for (int x = clipped.left(); x <= clipped.right(); ++x)
            if (image.pixel(x, y) != qRgb(255, 255, 255))
                ++ink;
    return ink;
}

QByteArray png(int w, int h, QColor color)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

/// Minimal HTTP/1.0 server: /ok.png (64x48), /redirect -> /ok.png, /slow.png
/// (answered on release()), anything else 404.
class HttpStub : public QObject
{
public:
    HttpStub()
    {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s] { serve(s); });
        });
        server.listen(QHostAddress::LocalHost);
    }
    QString url(const char* path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(QString::fromLatin1(path));
    }
    void release()
    {
        for (QTcpSocket* s : std::as_const(held))
            reply(s, "200 OK", png(30, 20, Qt::blue));
        held.clear();
    }

    QTcpServer server;
    QStringList requests;
    QList<QTcpSocket*> held;

private:
    void serve(QTcpSocket* s)
    {
        const QByteArray request = s->readAll();
        const QByteArray path = request.split(' ').value(1);
        requests << QString::fromLatin1(path);
        if (path == "/ok.png")
            reply(s, "200 OK", png(64, 48, Qt::red));
        else if (path == "/redirect")
            reply(s, "302 Found", {}, "Location: /ok.png\r\n");
        else if (path == "/slow.png")
            held << s;
        else
            reply(s, "404 Not Found", "nope");
    }
    static void reply(QTcpSocket* s, const char* status, const QByteArray& body, const char* extra = "")
    {
        s->write("HTTP/1.0 " + QByteArray(status) + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n"
                 + extra + "\r\n" + body);
        s->disconnectFromHost();
    }
};

class FixedRenderer : public EmbedRenderer
{
public:
    FixedRenderer(QString key, QSizeF size, bool acceptAll = true)
        : m_key(std::move(key)), m_size(size), m_all(acceptAll)
    {}
    QString key() const override { return m_key; }
    bool accepts(const QJsonValue& data) const override { return m_all || data.toString() == QLatin1String("mine"); }
    QSizeF size(const EmbedContext&) const override { return m_size; }
    void paint(QPainter* p, const QRectF& r, const EmbedContext&) const override { p->fillRect(r, Qt::green); }
    bool activate(QuillEditor*, const EmbedContext& c) override
    {
        activated << c.position;
        return consume;
    }
    QList<int> activated;
    bool consume = false;

private:
    QString m_key;
    QSizeF m_size;
    bool m_all;
};

class NotesRenderer : public CustomEmbedRenderer
{
public:
    NotesRenderer() : CustomEmbedRenderer(QStringLiteral("notes")) {}
    QSizeF size(const EmbedContext&) const override { return QSizeF(120, 40); }
    void paint(QPainter* p, const QRectF& r, const EmbedContext&) const override { p->fillRect(r, Qt::yellow); }
};

QSizeF embedSize(QTextDocument* doc, int pos)
{
    return charRect(doc, pos).size();
}

} // namespace

class TestEmbeds : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTrip_data()
    {
        QTest::addColumn<QString>("name");
        for (const QString& n : embedFixtures())
            QTest::newRow(qPrintable(n)) << n;
    }

    void roundTrip()
    {
        QFETCH(QString, name);
        const Delta input = canonical(fixture(name));
        QTextDocument doc;
        doc.setTextWidth(600);
        QVERIFY(DeltaDocument::load(&doc, input).ok);
        QCOMPARE(EmbedRegistry::registryOf(&doc), EmbedRegistry::shared());
        render(&doc); // drawing must not change anything
        QCOMPARE(QString::fromUtf8(DeltaDocument::toDelta(&doc).toJson()), QString::fromUtf8(input.toJson()));

        auto e = testutil::makeEditor(false);
        e->setContents(input);
        QCOMPARE(EmbedRegistry::registryOf(e->document()), e->embeds());
        QCOMPARE(QString::fromUtf8(e->getContents().toJson()), QString::fromUtf8(input.toJson()));
        // Byte-identical JSON for custom strings, including non-ASCII text.
        QCOMPARE(e->getContents().toJson(), input.toJson());
    }

    void everyEmbedDraws_data() { roundTrip_data(); }

    void everyEmbedDraws()
    {
        QFETCH(QString, name);
        const Delta input = fixture(name);
        QTextDocument doc;
        doc.setTextWidth(600);
        DeltaDocument::load(&doc, input);
        const QImage image = render(&doc);
        for (const int pos : embedPositions(input)) {
            const QRectF r = charRect(&doc, pos);
            QVERIFY2(r.width() >= 8 && r.height() >= 8, qPrintable(QStringLiteral("embed at %1 too small").arg(pos)));
            QVERIFY2(inkIn(image, r.toAlignedRect()) > 10, qPrintable(QStringLiteral("embed at %1 is blank").arg(pos)));
        }
    }

    void sizes()
    {
        QTextDocument doc;
        doc.setTextWidth(600);
        const QString dataPng =
            QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png(40, 20, Qt::red).toBase64());
        DeltaDocument::load(
            &doc, delta(QStringLiteral(
                            "[{'insert':{'image':'%1'}},{'insert':'\\n'},"
                            "{'insert':{'image':'%1'},'attributes':{'width':'100'}},{'insert':'\\n'},"
                            "{'insert':{'image':'%1'},'attributes':{'height':'10'}},{'insert':'\\n'},"
                            "{'insert':{'image':'%1'},'attributes':{'width':'2000','height':'1000'}},{'insert':'\\n'},"
                            "{'insert':{'image':'missing.png'}},{'insert':'\\n'},"
                            "{'insert':{'video':'v'}},{'insert':'\\n'},"
                            "{'insert':'x'},{'insert':{'video':'v'}},{'insert':'\\n'},"
                            "{'insert':{'video':'v'},'attributes':{'height':'90'}},{'insert':'\\n'},"
                            "{'insert':{'image':'%1'},'attributes':{'width':'20','height':'3000'}},{'insert':'\\n'}]")
                            .arg(dataPng)));
        const qreal avail = 600 - 2 * doc.documentMargin();
        QCOMPARE(embedSize(&doc, 0).width(), 40.0);    // natural size
        QCOMPARE(embedSize(&doc, 2), QSizeF(100, 50)); // width keeps the aspect ratio
        QCOMPARE(embedSize(&doc, 4).width(), 20.0);    // height keeps the aspect ratio
        QCOMPARE(embedSize(&doc, 6).width(), avail);   // shrunk to the line, aspect kept
        QCOMPARE(embedSize(&doc, 6).height(), avail / 2);
        QCOMPARE(embedSize(&doc, 8), QSizeF(ImageEmbed::PlaceholderWidth, ImageEmbed::PlaceholderHeight));
        QCOMPARE(embedSize(&doc, 10).width(), std::min(avail, 640.0)); // alone on its line
        QCOMPARE(embedSize(&doc, 13).width(), 320.0);                  // inline
        QCOMPARE(embedSize(&doc, 15).width(), 160.0);                  // 16:9 from the height
        // Very tall embeds make the line as tall.
        const QTextBlock tall = doc.findBlock(17);
        QVERIFY(doc.documentLayout()->blockBoundingRect(tall).height() >= 3000);
        QCOMPARE(QString::fromUtf8(DeltaDocument::toDelta(&doc).toJson()).count(QStringLiteral("{\"image\"")), 6);
    }

    void inlineEmbedsCentreOnText()
    {
        QTextDocument doc;
        DeltaDocument::load(&doc,
                            delta("[{'insert':'ab'},{'insert':{'formula':'x'}},{'insert':'cd'},"
                                  "{'insert':{'formula':'y'},'attributes':{'script':'super'}},{'insert':'\\n'}]"));
        const QRectF text = charRect(&doc, 0);
        const QRectF formula = charRect(&doc, 2);
        QVERIFY(formula.height() <= text.height() + 4);
        QCOMPARE(QString::fromUtf8(DeltaDocument::toDelta(&doc).toJson()),
                 QString::fromUtf8(delta("[{'insert':'ab'},{'insert':{'formula':'x'}},{'insert':'cd'},"
                                         "{'insert':{'formula':'y'},'attributes':{'script':'super'}},{'insert':'\\n'}]")
                                       .toJson()));
    }

    void registryPrecedence()
    {
        EmbedRegistry registry;
        auto* builtin = registry.rendererFor(QStringLiteral("video"), QStringLiteral("x"));
        QVERIFY(dynamic_cast<VideoEmbed*>(builtin));
        auto* picky = registry.emplace<FixedRenderer>(QStringLiteral("video"), QSizeF(50, 40), false);
        QCOMPARE(registry.rendererFor(QStringLiteral("video"), QStringLiteral("mine")), picky);
        QCOMPARE(registry.rendererFor(QStringLiteral("video"), QStringLiteral("other")), builtin);
        auto* all = registry.emplace<FixedRenderer>(QStringLiteral("video"), QSizeF(70, 30));
        QCOMPARE(registry.rendererFor(QStringLiteral("video"), QStringLiteral("mine")), all);
        QCOMPARE(registry.rendererFor(QStringLiteral("nope"), QJsonValue()), registry.fallback());
        QCOMPARE(registry.find<VideoEmbed>(), builtin);
        QCOMPARE(registry.find<FixedRenderer>(), all);
        // Images whose value is not a string are not images.
        QCOMPARE(registry.rendererFor(QStringLiteral("image"), QJsonObject()), registry.fallback());

        QTextDocument doc;
        LoadOptions options;
        options.embeds = &registry;
        DeltaDocument::load(&doc, delta("[{'insert':{'video':'a'}},{'insert':{'mention':1}},{'insert':'\\n'}]"),
                            options);
        QCOMPARE(EmbedRegistry::registryOf(&doc), &registry);
        QCOMPARE(embedSize(&doc, 0), QSizeF(70, 30));
        const QImage image = render(&doc);
        QCOMPARE(image.pixel(10, 10), QColor(Qt::green).rgb());
        QVERIFY(embedSize(&doc, 1).width() > 8); // generic chip
    }

    void customDispatch()
    {
        const auto parsed =
            CustomEmbedRenderer::parse(QStringLiteral("{\"notes\":\"[{\\\"insert\\\":\\\"é\\\\n\\\"}]\"}"));
        QVERIFY(parsed);
        QCOMPARE(parsed->first, QStringLiteral("notes"));
        QCOMPARE(parsed->second.toString(), QStringLiteral("[{\"insert\":\"é\\n\"}]"));
        QCOMPARE(CustomEmbedRenderer::encode(parsed->first, parsed->second),
                 QStringLiteral("{\"notes\":\"[{\\\"insert\\\":\\\"é\\\\n\\\"}]\"}"));
        QVERIFY(CustomEmbedRenderer::parse(QJsonObject{{QStringLiteral("poll"), 1}}));
        QVERIFY(!CustomEmbedRenderer::parse(QStringLiteral("not json")));
        QVERIFY(!CustomEmbedRenderer::parse(QStringLiteral("{\"a\":1,\"b\":2}")));

        EmbedRegistry registry;
        auto* notes = registry.emplace<NotesRenderer>();
        QVERIFY(notes->isBlock());
        QCOMPARE(registry.rendererFor(QStringLiteral("custom"), QStringLiteral("{\"notes\":\"x\"}")), notes);
        QCOMPARE(registry.rendererFor(QStringLiteral("custom"), QJsonObject{{QStringLiteral("notes"), 1}}), notes);
        QCOMPARE(registry.rendererFor(QStringLiteral("custom"), QStringLiteral("{\"poll\":\"x\"}")),
                 registry.fallback());
        QCOMPARE(registry.rendererFor(QStringLiteral("custom"), QStringLiteral("broken")), registry.fallback());

        QTextDocument doc;
        LoadOptions options;
        options.embeds = &registry;
        const Delta input = canonical(fixture(QStringLiteral("documents/embeds_custom_flutter_quill.json")));
        DeltaDocument::load(&doc, input, options);
        const QList<int> pos = embedPositions(input);
        QCOMPARE(embedSize(&doc, pos.at(0)), QSizeF(120, 40));
        QCOMPARE(embedSize(&doc, pos.at(1)), QSizeF(120, 40));
        QVERIFY(embedSize(&doc, pos.at(2)) != QSizeF(120, 40));
        // Fallback chips show the custom type.
        EmbedContext c = registry.context(&doc, pos.at(2), QTextCursor(&doc).charFormat());
        c.key = QStringLiteral("custom");
        c.data = QStringLiteral("{\"unknownType\":\"x\"}");
        QCOMPARE(GenericEmbed::label(c), QStringLiteral("unknownType"));
        c.data = QJsonObject{{QStringLiteral("poll"), 1}};
        QCOMPARE(GenericEmbed::label(c), QStringLiteral("poll"));
        QCOMPARE(DeltaDocument::toDelta(&doc).toJson(), input.toJson());
    }

    void remoteImages()
    {
        HttpStub http;
        QVERIFY(http.server.isListening());
        auto e = testutil::makeEditor(true);
        e->embeds()->setRemoteImagesEnabled(true);
        QSignalSpy loaded(e->embeds(), &EmbedRegistry::imageLoaded);
        e->setContents(delta(
            QStringLiteral("[{'insert':{'image':'%1'}},{'insert':'\\n'},{'insert':{'image':'%2'}},{'insert':'\\n'},"
                           "{'insert':{'image':'%3'},'attributes':{'width':'100','height':'80'}},{'insert':'\\n'}]")
                .arg(http.url("/ok.png"), http.url("/missing.png"), http.url("/redirect"))));
        QTextDocument* doc = e->document();
        const QString ok = http.url("/ok.png");
        // Not loaded yet: placeholder size.
        QCOMPARE(embedSize(doc, 0), QSizeF(ImageEmbed::PlaceholderWidth, ImageEmbed::PlaceholderHeight));
        QCOMPARE(e->embeds()->imageState(doc, ok), EmbedRegistry::ImageState::Loading);
        QSignalSpy relayout(doc->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged);
        QTRY_COMPARE(loaded.size(), 3);
        QCOMPARE(e->embeds()->imageState(doc, ok), EmbedRegistry::ImageState::Loaded);
        QCOMPARE(e->embeds()->imageState(doc, http.url("/missing.png")), EmbedRegistry::ImageState::Failed);
        QCOMPARE(e->embeds()->imageState(doc, http.url("/redirect")), EmbedRegistry::ImageState::Loaded);
        QVERIFY(relayout.size() > 0);
        QCOMPARE(embedSize(doc, 0), QSizeF(64, 48)); // natural size once loaded
        QCOMPARE(embedSize(doc, 2), QSizeF(ImageEmbed::PlaceholderWidth, ImageEmbed::PlaceholderHeight));
        QCOMPARE(embedSize(doc, 4), QSizeF(100, 80));
        const QImage image = render(doc);
        QCOMPARE(image.pixel(charRect(doc, 0).center().toPoint()), QColor(Qt::red).rgb());
        QCOMPARE(e->verificationFailures(), 0);
        QCOMPARE(e->getContents().length(), 6);

        // Cached: another document shows it at once, without a request.
        const int requests = int(http.requests.size());
        QTextDocument other;
        LoadOptions options;
        options.embeds = e->embeds();
        DeltaDocument::load(&other, delta(QStringLiteral("[{'insert':{'image':'%1'}},{'insert':'\\n'}]").arg(ok)),
                            options);
        QCOMPARE(embedSize(&other, 0), QSizeF(64, 48));
        QCOMPARE(http.requests.size(), requests);
        // Failures are remembered.
        e->setContents(
            delta(QStringLiteral("[{'insert':{'image':'%1'}},{'insert':'\\n'}]").arg(http.url("/missing.png"))));
        embedSize(e->document(), 0);
        QCOMPARE(http.requests.size(), requests);

        // The shared registry does not fetch.
        QTextDocument plain;
        DeltaDocument::load(
            &plain, delta(QStringLiteral("[{'insert':{'image':'%1'}},{'insert':'\\n'}]").arg(http.url("/slow.png"))));
        embedSize(&plain, 0);
        QTest::qWait(50);
        QCOMPARE(http.requests.size(), requests);
    }

    void remoteImageAfterDocumentIsGone()
    {
        HttpStub http;
        EmbedRegistry registry;
        registry.setRemoteImagesEnabled(true);
        QSignalSpy loaded(&registry, &EmbedRegistry::imageLoaded);
        {
            QTextDocument doc;
            doc.setTextWidth(400);
            LoadOptions options;
            options.embeds = &registry;
            DeltaDocument::load(
                &doc, delta(QStringLiteral("[{'insert':{'image':'%1'}},{'insert':'\\n'}]").arg(http.url("/slow.png"))),
                options);
            doc.size(); // lays out, starts the request
            QTRY_COMPARE(http.held.size(), 1);
        }
        http.release();
        QTRY_COMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().at(1).toBool(), true);
    }

    void activation()
    {
        auto e = testutil::makeEditor(true);
        auto* formula = e->embeds()->find<FormulaEmbed>();
        QVERIFY(formula);
        auto* custom = e->embeds()->emplace<FixedRenderer>(QStringLiteral("widget"), QSizeF(60, 30));
        const QString dataPng =
            QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png(40, 30, Qt::red).toBase64());
        e->setContents(
            delta(QStringLiteral("[{'insert':'f '},{'insert':{'formula':'x^2'}},{'insert':' text\\n'},"
                                 "{'insert':{'image':'%1'}},{'insert':'\\n'},{'insert':{'widget':1}},{'insert':'\\n'}]")
                      .arg(dataPng)));
        QSignalSpy activated(e.get(), &QuillEditor::embedActivated);
        const auto centre = [&](int pos) {
            QRectF r = charRect(e->document(), pos);
            r.translate(-e->horizontalScrollBar()->value(), -e->verticalScrollBar()->value());
            return r.center().toPoint();
        };
        QCOMPARE(e->embedAt(centre(2)), 2);
        QCOMPARE(e->embedAt(centre(0)), -1);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, centre(2));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(activated.at(0).at(0).toString(), QStringLiteral("formula"));
        QCOMPARE(activated.at(0).at(1).value<QJsonValue>().toString(), QStringLiteral("x^2"));
        QCOMPARE(activated.at(0).at(2).toInt(), 2);
        // Images activate on a double click only.
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, centre(9));
        QCOMPARE(activated.size(), 1);
        QTest::mouseDClick(e->viewport(), Qt::LeftButton, {}, centre(9));
        QCOMPARE(activated.size(), 2);
        QCOMPARE(activated.at(1).at(0).toString(), QStringLiteral("image"));
        // A renderer may handle activation itself.
        custom->consume = true;
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, centre(11));
        QCOMPARE(custom->activated, QList<int>{11});
        QCOMPARE(activated.size(), 2);
        custom->consume = false;
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, centre(11));
        QCOMPARE(activated.size(), 3);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void insertBlockEmbed()
    {
        auto e = testutil::makeEditor(false);
        testutil::ChangeRecorder rec(e.get());
        e->setContents(delta("[{'insert':'abcd'},{'insert':'\\n','attributes':{'header':1}}]"));
        // Middle of a line: split, the first half keeps the line format.
        Delta change = e->insertBlockEmbed(2, QStringLiteral("video"), QStringLiteral("v"));
        QCOMPARE(
            QString::fromUtf8(change.toJson()),
            QString::fromUtf8(
                delta(
                    "[{'retain':2},{'insert':'\\n','attributes':{'header':1}},{'insert':{'video':'v'}},{'insert':'\\n'}]")
                    .toJson()));
        // Line start: only a newline after it.
        change = e->insertBlockEmbed(0, QStringLiteral("image"), QStringLiteral("i"));
        QCOMPARE(QString::fromUtf8(change.toJson()),
                 QString::fromUtf8(delta("[{'insert':{'image':'i'}},{'insert':'\\n'}]").toJson()));
        // Already on an empty line: just the embed.
        e->setContents(delta("[{'insert':'a\\n\\nb\\n'}]"));
        change = e->insertBlockEmbed(2, QStringLiteral("image"), QStringLiteral("i"));
        QCOMPARE(QString::fromUtf8(change.toJson()),
                 QString::fromUtf8(delta("[{'retain':2},{'insert':{'image':'i'}}]").toJson()));
        // Line end: a newline before it, keeping the line's format.
        e->setContents(delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}}]"));
        change = e->insertBlockEmbed(2, QStringLiteral("image"), QStringLiteral("i"));
        QCOMPARE(QString::fromUtf8(e->getContents().toJson()),
                 QString::fromUtf8(
                     delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':{'image':'i'}},"
                           "{'insert':'\\n','attributes':{'list':'bullet'}}]")
                         .toJson()));
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void typingAfterAnInlineEmbedIsNotCentred()
    {
        auto e = testutil::makeEditor(true);
        e->setContents(delta("[{'insert':'a'},{'insert':{'formula':'x'}},{'insert':'\\n'}]"));
        e->setSelection(2);
        QTest::keyClicks(e.get(), QStringLiteral("bc"));
        QCOMPARE(QString::fromUtf8(e->getContents().toJson()),
                 QString::fromUtf8(delta("[{'insert':'a'},{'insert':{'formula':'x'}},{'insert':'bc\\n'}]").toJson()));
        QTextCursor c(e->document());
        c.setPosition(3);
        QCOMPARE(c.charFormat().verticalAlignment(), QTextCharFormat::AlignNormal);
        QCOMPARE(e->verificationFailures(), 0);
    }
};

QTEST_MAIN(TestEmbeds)
#include "tst_embeds.moc"
