// Image resize handles (M13): attribute spelling (flutter_quill_extensions'
// style string, Quill.js width/height), mouse drag, limits, alignment,
// context menu, read-only.
#include "EditorTestUtil.h"
#include "ImageSize.h"
#include "QuillEditor_p.h"

#include <QBuffer>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

using quill::AttributeMap;
using quill::Delta;
using quill::QuillEditor;
using quill::Range;
using quill::Source;
using testutil::delta;

namespace {

QString pngUrl(int w, int h, QColor color)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

const QColor kGreen(0, 170, 0);

/// "text\n" + an image (200 x 100 unless sized by `attrs`) alone on its line + "after\n".
Delta imageDoc(const AttributeMap& attrs = {}, const AttributeMap& line = {})
{
    return Delta()
        .insert("text\n")
        .insert(QJsonObject{{"image", pngUrl(200, 100, kGreen)}}, attrs)
        .insert("\n", line)
        .insert("after\n");
}

constexpr int kImage = 5; // index of the image in imageDoc()

QPoint at(const QPointF& p)
{
    return p.toPoint();
}

} // namespace

class TestImageResize : public QObject
{
    Q_OBJECT

    std::unique_ptr<QuillEditor> e;
    quill::QuillEditorPrivate* d = nullptr;

    void start(const AttributeMap& attrs = {}, const AttributeMap& line = {})
    {
        e = testutil::makeEditor();
        e->resize(700, 500);
        e->setContents(imageDoc(attrs, line));
        d = quill::QuillEditorPrivate::get(e.get());
        QCoreApplication::processEvents();
    }

    /// Drags the corner `handle` of the selected image by `delta`.
    void drag(int handle, QPointF by, Qt::KeyboardModifiers mods = {}, bool escape = false)
    {
        const QRectF r = d->embedRect(kImage);
        const QPointF from = handle == 0   ? r.topLeft()
                             : handle == 1 ? r.topRight()
                             : handle == 2 ? r.bottomRight()
                                           : r.bottomLeft();
        QCOMPARE(d->handleAt(at(from)), handle);
        QTest::mousePress(e->viewport(), Qt::LeftButton, mods, at(from));
        const QPointF to = from + by;
        for (int i = 1; i <= 4; ++i) {
            const QPointF p = from + by * (i / 4.0);
            QMouseEvent move(QEvent::MouseMove, p, e->viewport()->mapToGlobal(p), Qt::NoButton, Qt::LeftButton, mods);
            QApplication::sendEvent(e->viewport(), &move);
        }
        e->viewport()->grab().save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/image_dragging.png"));
        if (escape)
            QTest::keyClick(e.get(), Qt::Key_Escape);
        QTest::mouseRelease(e->viewport(), Qt::LeftButton, mods, at(to));
    }

    void select()
    {
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, at(d->embedRect(kImage).center()));
        QCOMPARE(e->getSelection(), (Range{kImage, 1}));
        QCOMPARE(d->selectedEmbed(), kImage);
    }

    AttributeMap imageAttrs() const { return e->getContents(kImage, 1).ops().first().attributes(); }

private slots:
    void cleanup()
    {
        e.reset();
        d = nullptr;
    }

    // --- Attributes ----------------------------------------------------------------

    void styleString()
    {
        using quill::detail::replaceStyleStringWithSize;
        // flutter_quill_extensions 11.0.0, common/utils/string.dart.
        QCOMPARE(replaceStyleStringWithSize("", 240, 135), QString("width: 240.0; height: 135.0; "));
        QCOMPARE(replaceStyleStringWithSize("margin: 4px;width:100;  ;cursor", 240, 135),
                 QString("margin: 4px; width: 240.0; height: 135.0; "));
        QCOMPARE(replaceStyleStringWithSize("height: 9; mobileAlignment: center; height: 10", 30, 20),
                 QString("height: 20.0; mobileAlignment: center; width: 30.0; "));
    }

    void spellings()
    {
        using quill::detail::resizedImageAttributes;
        using quill::detail::SizeSpelling;
        const QSizeF size(240.4, 134.6);
        const QJsonValue null(QJsonValue::Null);
        // No size yet: flutter_quill's style.
        QCOMPARE(resizedImageAttributes({}, size, SizeSpelling::Auto),
                 (AttributeMap{{"style", "width: 240.0; height: 135.0; "}}));
        // Quill.js attributes stay Quill.js, with their types.
        QCOMPARE(resizedImageAttributes({{"width", "100"}}, size, SizeSpelling::Auto),
                 (AttributeMap{{"width", "240"}, {"height", "135"}}));
        QCOMPARE(resizedImageAttributes({{"width", 100}, {"height", 50}}, size, SizeSpelling::Auto),
                 (AttributeMap{{"width", 240}, {"height", 135}}));
        // A size in style wins: write style, keep its other declarations.
        QCOMPARE(
            resizedImageAttributes({{"width", "100"}, {"style", "width: 100; margin: 2"}}, size, SizeSpelling::Auto),
            (AttributeMap{{"style", "width: 240.0; margin: 2; height: 135.0; "}}));
        // Forced Quill.js: the size leaves the style, which would win.
        QCOMPARE(resizedImageAttributes({{"style", "width: 100; margin: 2"}}, size, SizeSpelling::QuillJs),
                 (AttributeMap{{"width", "240"}, {"height", "135"}, {"style", "margin: 2; "}}));
        QCOMPARE(resizedImageAttributes({{"style", "width: 100"}}, size, SizeSpelling::QuillJs),
                 (AttributeMap{{"width", "240"}, {"height", "135"}, {"style", null}}));
        // Natural size: every size removed, other declarations kept.
        QCOMPARE(resizedImageAttributes({{"style", "margin: 2; width: 9; height: 9"}, {"width", "5"}}, std::nullopt,
                                        SizeSpelling::Auto),
                 (AttributeMap{{"style", "margin: 2; "}, {"width", null}}));
        QCOMPARE(resizedImageAttributes({{"bold", true}}, std::nullopt, SizeSpelling::Auto), AttributeMap());
    }

    // --- Mouse ----------------------------------------------------------------------

    void clickSelects()
    {
        start();
        QCOMPARE(d->selectedEmbed(), -1);
        select();
        // Qt's selection highlight does not cover the image: the image is drawn over it.
        const QImage grab = e->viewport()->grab().toImage();
        const qreal dpr = grab.devicePixelRatio();
        const QPointF c = d->embedRect(kImage).center() + QPointF(20, 0);
        QCOMPARE(grab.pixelColor(int(c.x() * dpr), int(c.y() * dpr)), kGreen);
        // The frame: the accent colour on the image's edge.
        const QRectF r = d->embedRect(kImage);
        const QColor accent = e->palette().color(QPalette::Highlight);
        QCOMPARE(grab.pixelColor(int(r.center().x() * dpr), int((r.top() + 0.5) * dpr)), accent);
        QCOMPARE(grab.pixelColor(int(r.center().x() * dpr), int((r.bottom() - 0.5) * dpr)), accent);
        QCOMPARE(grab.pixelColor(int((r.left() + 0.5) * dpr), int(r.center().y() * dpr)), accent);
        QCOMPARE(grab.pixelColor(int((r.right() - 0.5) * dpr), int(r.center().y() * dpr)), accent);
        // No caret beside it, no highlight left below it.
        QVERIFY(grab.pixelColor(int((r.right() + 0.5) * dpr), int(r.center().y() * dpr)).lightness() > 200);
        QVERIFY(grab.pixelColor(int(r.center().x() * dpr), int((r.bottom() + 1.5) * dpr)).lightness() > 200);
        grab.save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/image_selected.png"));
        // Escape, typing or clicking elsewhere deselects.
        QTest::keyClick(e.get(), Qt::Key_Escape);
        QCOMPARE(e->getSelection(), (Range{kImage + 1, 0}));
        QCOMPARE(d->selectedEmbed(), -1);
        select();
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, at(d->embedRect(kImage).bottomRight() + QPointF(150, 40)));
        QCOMPARE(d->selectedEmbed(), -1);
        // Double click still activates.
        QSignalSpy activated(e.get(), &QuillEditor::embedActivated);
        QTest::mouseDClick(e->viewport(), Qt::LeftButton, {}, at(d->embedRect(kImage).center()));
        QCOMPARE(activated.size(), 1);
    }

    void resizeDrag()
    {
        start();
        testutil::ChangeRecorder rec(e.get());
        select();
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(200, 100));
        drag(2, QPointF(100, 10)); // aspect kept: the larger relative move wins
        QCOMPARE(rec.records.size(), 1);
        QCOMPARE(rec.last().source, Source::User);
        QCOMPARE(rec.last().change.toJson(),
                 delta("[{'retain':5},{'retain':1,'attributes':{'style':'width: 300.0; height: 150.0; '}}]").toJson());
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(300, 150));
        // The frame follows the new layout; the image stays selected.
        QCOMPARE(d->selectedEmbed(), kImage);
        // One undo step.
        e->undo();
        QCOMPARE(imageAttrs(), AttributeMap());
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(200, 100));
        // From the top-left corner, shrinking.
        e->setSelection(kImage, 1, Source::User);
        drag(0, QPointF(50, 0));
        QCOMPARE(imageAttrs(), (AttributeMap{{"style", "width: 150.0; height: 75.0; "}}));
        QCOMPARE(rec.mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
    }

    void resizeShiftAndLimits()
    {
        start({{"style", "margin: 3px"}});
        select();
        drag(2, QPointF(-20, 60), Qt::ShiftModifier);
        QCOMPARE(imageAttrs(), (AttributeMap{{"style", "margin: 3px; width: 180.0; height: 160.0; "}}));
        e->setSelection(kImage, 1, Source::User);
        drag(2, QPointF(-500, -500), Qt::ShiftModifier);
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(16, 16));
        // Not wider than the line.
        e->setSelection(kImage, 1, Source::User);
        drag(2, QPointF(2000, 0));
        const qreal width = d->availableWidth(kImage);
        QCOMPARE(d->embedRect(kImage).width(), std::round(width));
        QVERIFY(width < 700);
    }

    void escapeCancelsDrag()
    {
        start();
        testutil::ChangeRecorder rec(e.get());
        select();
        drag(2, QPointF(80, 0), {}, true);
        QCOMPARE(rec.records.size(), 0);
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(200, 100));
    }

    void quillJsSpellingKept()
    {
        start({{"width", "100"}, {"alt", "x"}});
        select();
        drag(2, QPointF(50, 0));
        QCOMPARE(imageAttrs(), (AttributeMap{{"width", "150"}, {"height", "75"}, {"alt", "x"}}));
        e->setImageSizeAttributes(QuillEditor::ImageSizeAttributes::FlutterQuill);
        e->setSelection(kImage, 1, Source::User);
        drag(2, QPointF(50, 0));
        QCOMPARE(imageAttrs().value("style").toString(), QString("width: 200.0; height: 100.0; "));
    }

    void alignedAndRtl_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("left") << "{}";
        QTest::newRow("center") << "{'align':'center'}";
        QTest::newRow("right") << "{'align':'right'}";
        QTest::newRow("rtl") << "{'direction':'rtl','align':'right'}";
    }

    void alignedAndRtl()
    {
        QFETCH(QString, line);
        start({}, delta(QStringLiteral("[{'retain':1,'attributes':%1}]").arg(line).toUtf8().constData())
                      .ops()
                      .first()
                      .attributes());
        const QRectF r = d->embedRect(kImage);
        QCOMPARE(r.size(), QSizeF(200, 100));
        // The rect is where the image is painted.
        const QImage grab = e->viewport()->grab().toImage();
        const qreal dpr = grab.devicePixelRatio();
        const auto px = [&](qreal x, qreal y) { return grab.pixelColor(int(x * dpr), int(y * dpr)); };
        QCOMPARE(px(r.left() + 2, r.top() + 2), kGreen);
        QCOMPARE(px(r.right() - 2, r.bottom() - 2), kGreen);
        QVERIFY(px(r.left() - 3, r.center().y()) != kGreen);
        QVERIFY(px(r.right() + 3, r.center().y()) != kGreen);
        const qreal mid = e->viewport()->width() / 2.0;
        if (line.contains("center"))
            QVERIFY(std::abs(r.center().x() - mid) < 3);
        if (line.contains("right"))
            QVERIFY(r.left() > mid);
        // Handles at its corners, and they resize.
        select();
        QCOMPARE(d->handleAt(at(r.topRight())), 1);
        QCOMPARE(d->handleAt(at(r.bottomLeft())), 3);
        drag(1, QPointF(40, 0));
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(240, 120));
    }

    void contextMenu()
    {
        start({{"style", "margin: 1; width: 120; height: 30"}});
        select();
        const auto trigger = [&](const QString& text) {
            const std::unique_ptr<QMenu> menu(d->contextMenu(at(d->embedRect(kImage).center())));
            QAction* entry = menu->findChild<QAction*>("image-size");
            QVERIFY(entry && entry->menu());
            for (QAction* a : entry->menu()->actions()) {
                if (a->text() == text) {
                    a->trigger();
                    return;
                }
            }
            QFAIL(qPrintable("no action " + text));
        };
        trigger("Original size");
        QCOMPARE(imageAttrs(), (AttributeMap{{"style", "margin: 1; "}}));
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(200, 100));
        trigger("50 %");
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(100, 50));
        trigger("Fit width");
        QCOMPARE(d->embedRect(kImage).width(), std::round(d->availableWidth(kImage)));
        trigger("100 %");
        QCOMPARE(d->embedRect(kImage).size(), QSizeF(200, 100));
        QCOMPARE(d->selectedEmbed(), kImage);
        // Not offered without a selected image.
        e->setSelection(0, 0, Source::User);
        const std::unique_ptr<QMenu> plain(d->contextMenu(QPoint(5, 5)));
        QVERIFY(!plain->findChild<QAction*>("image-size"));
    }

    void noHandlesReadOnlyOrDisabled()
    {
        start();
        e->setReadOnly(true);
        e->setSelection(kImage, 1, Source::User);
        QCOMPARE(d->selectedEmbed(), -1);
        QCOMPARE(d->handleAt(at(d->embedRect(kImage).bottomRight())), -1);
        e->setReadOnly(false);
        e->setImageResizeEnabled(false);
        QCOMPARE(d->selectedEmbed(), -1);
        QTest::mouseClick(e->viewport(), Qt::LeftButton, {}, at(d->embedRect(kImage).center()));
        QVERIFY(e->getSelection().length == 0); // a plain click places the caret
        e->setImageResizeEnabled(true);
        // Videos are not resizable.
        e->setContents(Delta().insert(QJsonObject{{"video", "https://v.example/a.mp4"}}).insert("\n"));
        e->setSelection(0, 1, Source::User);
        QCOMPARE(d->selectedEmbed(), -1);
    }

    void resizeEmbedApi()
    {
        start();
        const Delta change = e->resizeEmbed(kImage, QSizeF(60.4, 30.2));
        QCOMPARE(change.toJson(),
                 delta("[{'retain':5},{'retain':1,'attributes':{'style':'width: 60.0; height: 30.0; '}}]").toJson());
        QCOMPARE(e->resizeEmbed(0, QSizeF(10, 10)).isEmpty(), true); // not an embed
        e->resizeEmbed(kImage, QSizeF());
        QCOMPARE(imageAttrs(), AttributeMap());
    }
};

QTEST_MAIN(TestImageResize)
#include "tst_image_resize.moc"
