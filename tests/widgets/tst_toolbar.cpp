// QuillToolbar and QuillFindBar: each control produces the expected change
// Delta, and the controls follow the selection.

#include "EditorTestUtil.h"

#include "ColorButton.h"
#include "Icons.h"

#include <quill/QuillFindBar.h>
#include <quill/QuillToolbar.h>

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QVBoxLayout>

using namespace quill;
using testutil::ChangeRecorder;
using testutil::delta;

namespace {

void check(const Delta& actual, const Delta& expected, int line)
{
    if (actual != expected)
        qWarning("line %d\n  actual:   %s\n  expected: %s", line, actual.toJson().constData(),
                 expected.toJson().constData());
    QTest::qCompare(actual, expected, "actual", "expected", __FILE__, line);
}

/// Toolbar whose dialogs return scripted answers.
class ScriptedToolbar : public QuillToolbar
{
public:
    using QuillToolbar::QuillToolbar;
    std::optional<LinkEdit> linkAnswer;
    LinkEdit linkAsked;
    int linkPrompts = 0;
    QString imageAnswer, videoAnswer, formulaAnswer;
    std::optional<QColor> colorAnswer;

protected:
    std::optional<LinkEdit> promptLink(const LinkEdit& current) override
    {
        ++linkPrompts;
        linkAsked = current;
        return linkAnswer;
    }
    QString promptImage() override { return imageAnswer; }
    QString promptVideo() override { return videoAnswer; }
    QString promptFormula() override { return formulaAnswer; }
    std::optional<QColor> promptColor(const QColor&, const QString&) override { return colorAnswer; }
};

} // namespace

class TestToolbar : public QObject
{
    Q_OBJECT

    std::unique_ptr<QWidget> window;
    QuillEditor* e = nullptr;
    ScriptedToolbar* bar = nullptr;
    QuillFindBar* find = nullptr;
    std::unique_ptr<ChangeRecorder> rec;

    void start(const char* json, int index = 0, int length = 0)
    {
        e->setContents(delta(json));
        e->setSelection(index, length);
        rec->clear();
    }
    QAction* a(const char* name) { return bar->action(QString::fromLatin1(name)); }
    QComboBox* combo(const char* name) { return bar->findChild<QComboBox*>(QString::fromLatin1(name)); }
    detail::ColorButton* colorButton(const char* name)
    {
        return bar->findChild<detail::ColorButton*>(QString::fromLatin1(name));
    }

private slots:
    void init()
    {
        window = std::make_unique<QWidget>();
        auto* layout = new QVBoxLayout(window.get());
        e = new QuillEditor(window.get());
        e->setVerifyChanges(true);
        bar = new ScriptedToolbar(e, window.get());
        find = new QuillFindBar(window.get());
        find->hide();
        bar->setFindBar(find);
        layout->addWidget(bar);
        layout->addWidget(e);
        layout->addWidget(find);
        window->resize(1000, 500);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        rec = std::make_unique<ChangeRecorder>(e);
    }

    void cleanup()
    {
        QCOMPARE(rec->mismatches, 0);
        QCOMPARE(e->verificationFailures(), 0);
        rec.reset();
        window.reset();
    }

    void actionsAndGroups()
    {
        const QStringList expected{"undo", "redo", "font", "size", "bold", "italic", "underline", "strike", "code",
                                   "subscript", "superscript", "small", "color", "background", "clear", "header",
                                   "list-bullet", "list-ordered", "list-check", "outdent", "indent", "align-left",
                                   "align-center", "align-right", "align-justify", "direction-rtl", "blockquote",
                                   "code-block", "link", "image", "video", "formula", "search"};
        QCOMPARE(bar->actionNames(), expected);
        QStringList grouped;
        for (const QString& g : QuillToolbar::groupNames())
            grouped += bar->groupActions(g);
        QCOMPARE(grouped, expected);
        for (const QString& name : expected) {
            QVERIFY2(bar->action(name), qPrintable(name));
            if (!bar->action(name)->icon().isNull() || name == QLatin1String("font") || name == QLatin1String("size")
                || name == QLatin1String("header") || name == QLatin1String("color") || name == QLatin1String("background"))
                continue;
            QFAIL(qPrintable(QStringLiteral("no icon for ") + name));
        }
        QVERIFY(!a("small")->isVisible()); // flutter_quill hides it by default
        QVERIFY(a("search")->isVisible());
        bar->setGroupVisible(QStringLiteral("align"), false);
        QVERIFY(!a("align-center")->isVisible());
        bar->setGroupVisible(QStringLiteral("align"), true);
        QVERIFY(a("align-center")->isVisible());
        QuillToolbar plain;
        QVERIFY(!plain.action(QStringLiteral("search"))->isVisible()); // no find bar
        QVERIFY(!plain.action(QStringLiteral("bold"))->isEnabled());   // no editor
    }

    void inlineToggles()
    {
        start("[{'insert':'hello world\\n'}]", 0, 5);
        a("bold")->trigger();
        check(rec->last().change, delta("[{'retain':5,'attributes':{'bold':true}}]"), __LINE__);
        QCOMPARE(rec->last().source, Source::User);
        QVERIFY(a("bold")->isChecked());
        a("italic")->trigger();
        a("underline")->trigger();
        a("strike")->trigger();
        a("code")->trigger();
        check(e->getContents(), delta("[{'insert':'hello','attributes':{'bold':true,'code':true,'italic':true,'strike':true,'underline':true}},{'insert':' world\\n'}]"), __LINE__);
        a("bold")->trigger(); // off
        QVERIFY(!a("bold")->isChecked());
        check(rec->last().change, delta("[{'retain':5,'attributes':{'bold':null}}]"), __LINE__);
        // State follows the selection.
        e->setSelection(8, 0);
        QVERIFY(!a("italic")->isChecked());
        e->setSelection(2, 0);
        QVERIFY(a("italic")->isChecked());
        // Mixed selection: unchecked.
        e->setSelection(3, 5);
        QVERIFY(!a("italic")->isChecked());
    }

    void collapsedToggleAppliesToTyping()
    {
        start("[{'insert':'ab\\n'}]", 1);
        a("bold")->trigger();
        QVERIFY(a("bold")->isChecked());
        QCOMPARE(rec->records.size(), 0);
        QTest::keyClicks(e, QStringLiteral("X"));
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'X','attributes':{'bold':true}},{'insert':'b\\n'}]"), __LINE__);
    }

    void scriptIsExclusive()
    {
        start("[{'insert':'x2\\n'}]", 1, 1);
        a("superscript")->trigger();
        QVERIFY(a("superscript")->isChecked());
        a("subscript")->trigger();
        QVERIFY(a("subscript")->isChecked());
        QVERIFY(!a("superscript")->isChecked());
        check(e->getContents(), delta("[{'insert':'x'},{'insert':'2','attributes':{'script':'sub'}},{'insert':'\\n'}]"), __LINE__);
    }

    void listsAndIndent()
    {
        start("[{'insert':'a\\nb\\n'}]", 0, 3);
        a("list-ordered")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'ordered'}},{'insert':'b'},{'insert':'\\n','attributes':{'list':'ordered'}}]"), __LINE__);
        QVERIFY(a("list-ordered")->isChecked());
        a("list-bullet")->trigger();
        QVERIFY(a("list-bullet")->isChecked());
        QVERIFY(!a("list-ordered")->isChecked());
        a("indent")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'indent':1,'list':'bullet'}},{'insert':'b'},{'insert':'\\n','attributes':{'indent':1,'list':'bullet'}}]"), __LINE__);
        a("outdent")->trigger();
        a("list-check")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'list':'unchecked'}},{'insert':'b'},{'insert':'\\n','attributes':{'list':'unchecked'}}]"), __LINE__);
        QVERIFY(a("list-check")->isChecked());
        a("list-check")->trigger(); // off
        check(e->getContents(), delta("[{'insert':'a\\nb\\n'}]"), __LINE__);
    }

    void alignmentAndBlocks()
    {
        start("[{'insert':'a\\n'}]");
        QVERIFY(a("align-left")->isChecked());
        a("align-center")->trigger();
        QVERIFY(a("align-center")->isChecked());
        QVERIFY(!a("align-left")->isChecked());
        a("direction-rtl")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'align':'center','direction':'rtl'}}]"), __LINE__);
        a("align-left")->trigger();
        a("direction-rtl")->trigger();
        check(e->getContents(), delta("[{'insert':'a\\n'}]"), __LINE__);
        a("blockquote")->trigger();
        QVERIFY(a("blockquote")->isChecked());
        a("code-block")->trigger(); // replaces the quote
        QVERIFY(!a("blockquote")->isChecked());
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'code-block':true}}]"), __LINE__);
    }

    void headerCombo()
    {
        start("[{'insert':'a'},{'insert':'\\n','attributes':{'header':2}},{'insert':'b\\n'}]");
        QCOMPARE(combo("header")->currentIndex(), 2);
        e->setSelection(3, 0);
        QCOMPARE(combo("header")->currentIndex(), 0); // Normal
        Q_EMIT combo("header")->activated(1);
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'header':2}},{'insert':'b'},{'insert':'\\n','attributes':{'header':1}}]"), __LINE__);
        e->setSelection(0, 4);
        QCOMPARE(combo("header")->currentIndex(), -1); // mixed
        Q_EMIT combo("header")->activated(0);
        check(e->getContents(), delta("[{'insert':'a\\nb\\n'}]"), __LINE__);
        QVERIFY(e->hasFocus());
    }

    void fontAndSizeCombos()
    {
        start("[{'insert':'abc','attributes':{'size':'15.0'}},{'insert':'\\n'}]", 1, 1);
        QCOMPARE(combo("size")->currentText(), QStringLiteral("15.0")); // added for an unlisted size
        QCOMPARE(combo("font")->currentIndex(), -1);
        Q_EMIT combo("font")->activated(1); // Serif
        Q_EMIT combo("size")->activated(1); // Large
        check(e->getContents(), delta("[{'insert':'a','attributes':{'size':'15.0'}},{'insert':'b','attributes':{'font':'serif','size':'large'}},"
                                      "{'insert':'c','attributes':{'size':'15.0'}},{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(combo("font")->currentText(), QStringLiteral("Serif"));
        Q_EMIT combo("size")->activated(combo("size")->findText(QStringLiteral("24")));
        check(e->getContents().slice(1, 2), delta("[{'insert':'b','attributes':{'font':'serif','size':'24'}}]"), __LINE__);
        Q_EMIT combo("font")->activated(combo("font")->findText(QStringLiteral("Clear")));
        Q_EMIT combo("size")->activated(combo("size")->findText(QStringLiteral("Clear")));
        check(e->getContents().slice(1, 2), delta("[{'insert':'b'}]"), __LINE__);
        bar->setFontFamilies({{QStringLiteral("Mono"), QStringLiteral("monospace")}});
        QCOMPARE(combo("font")->count(), 1);
    }

    void colors()
    {
        start("[{'insert':'abc\\n'}]", 0, 2);
        colorButton("color")->selectColor(QColor(0xF4, 0x43, 0x36));
        check(rec->last().change, delta("[{'retain':2,'attributes':{'color':'#FFF44336'}}]"), __LINE__);
        QCOMPARE(colorButton("color")->currentColor(), std::optional<QColor>(QColor(0xF4, 0x43, 0x36)));
        bar->colorAnswer = QColor(0, 0, 255);
        Q_EMIT colorButton("background")->moreRequested();
        check(rec->last().change, delta("[{'retain':2,'attributes':{'background':'#FF0000FF'}}]"), __LINE__);
        colorButton("color")->selectColor(std::nullopt);
        check(e->getContents(), delta("[{'insert':'ab','attributes':{'background':'#FF0000FF'}},{'insert':'c\\n'}]"), __LINE__);
        QVERIFY(!colorButton("color")->currentColor());
        // CSS dialect writes CSS colours.
        StyleOptions css = e->styleOptions();
        css.colorDialect = ColorDialect::Css;
        e->setStyleOptions(css);
        e->setSelection(2, 1);
        colorButton("color")->selectColor(QColor(0xF4, 0x43, 0x36));
        check(e->getContents().slice(2, 3), delta("[{'insert':'c','attributes':{'color':'#f44336'}}]"), __LINE__);
        QCOMPARE(colorButton("color")->colors().size(), 21);
    }

    void clearFormatting()
    {
        start("[{'insert':'ab','attributes':{'bold':true,'italic':true}},{'insert':'\\n','attributes':{'header':1}}]", 0, 1);
        a("clear")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'b','attributes':{'bold':true,'italic':true}},{'insert':'\\n'}]"), __LINE__);
        // Collapsed: clears the typing format only.
        e->setSelection(2, 0);
        a("clear")->trigger();
        QTest::keyClicks(e, QStringLiteral("c"));
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'b','attributes':{'bold':true,'italic':true}},{'insert':'c\\n'}]"), __LINE__);
    }

    void links()
    {
        // Collapsed, no link: inserts linked text.
        start("[{'insert':'see \\n'}]", 4);
        bar->linkAnswer = QuillToolbar::LinkEdit{QStringLiteral("docs"), QStringLiteral("https://d"), false};
        a("link")->trigger();
        check(e->getContents(), delta("[{'insert':'see '},{'insert':'docs','attributes':{'link':'https://d'}},{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{8, 0}));
        // Collapsed inside a link: the whole link is edited.
        e->setSelection(6, 0);
        QVERIFY(a("link")->isChecked());
        bar->linkAnswer = QuillToolbar::LinkEdit{QStringLiteral("docs"), QStringLiteral("https://new"), false};
        a("link")->trigger();
        QCOMPARE(bar->linkAsked.text, QStringLiteral("docs"));
        QCOMPARE(bar->linkAsked.url, QStringLiteral("https://d"));
        check(e->getContents(), delta("[{'insert':'see '},{'insert':'docs','attributes':{'link':'https://new'}},{'insert':'\\n'}]"), __LINE__);
        // Changed text replaces it.
        e->setSelection(5, 0);
        bar->linkAnswer = QuillToolbar::LinkEdit{QStringLiteral("manual"), QStringLiteral("https://new"), false};
        a("link")->trigger();
        check(e->getContents(), delta("[{'insert':'see '},{'insert':'manual','attributes':{'link':'https://new'}},{'insert':'\\n'}]"), __LINE__);
        // Remove.
        e->setSelection(5, 0);
        bar->linkAnswer = QuillToolbar::LinkEdit{QString(), QString(), true};
        a("link")->trigger();
        check(e->getContents(), delta("[{'insert':'see manual\\n'}]"), __LINE__);
        // Selection: links it; cancel changes nothing; Ctrl+K opens the prompt.
        e->setSelection(0, 3);
        bar->linkAnswer = std::nullopt;
        const int before = bar->linkPrompts;
        QTest::keyClick(e, Qt::Key_K, Qt::ControlModifier);
        QCOMPARE(bar->linkPrompts, before + 1);
        QCOMPARE(bar->linkAsked.text, QStringLiteral("see"));
        bar->linkAnswer = QuillToolbar::LinkEdit{QStringLiteral("see"), QStringLiteral("https://s"), false};
        a("link")->trigger();
        check(e->getContents(), delta("[{'insert':'see','attributes':{'link':'https://s'}},{'insert':' manual\\n'}]"), __LINE__);
    }

    void embeds()
    {
        // Images and videos are block embeds: they get a line of their own
        // (flutter_quill's InsertEmbedsRule); formulas stay inline.
        start("[{'insert':'ab'},{'insert':'\\n','attributes':{'header':2}}]", 1, 1);
        bar->imageAnswer = QStringLiteral("file:///tmp/x.png");
        a("image")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'header':2}},{'insert':{'image':'file:///tmp/x.png'}},"
                                      "{'insert':'\\n','attributes':{'header':2}}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{3, 0}));
        e->formatLine(3, 0, AttributeMap{{QStringLiteral("header"), QJsonValue::Null}});
        bar->videoAnswer = QStringLiteral("https://v");
        a("video")->trigger();
        check(e->getContents(), delta("[{'insert':'a'},{'insert':'\\n','attributes':{'header':2}},{'insert':{'image':'file:///tmp/x.png'}},"
                                      "{'insert':'\\n'},{'insert':{'video':'https://v'}},{'insert':'\\n'}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{5, 0}));
        e->insertText(5, QStringLiteral(" x"));
        e->setSelection(6, 0);
        bar->formulaAnswer = QStringLiteral("e=mc^2");
        a("formula")->trigger();
        bar->formulaAnswer.clear(); // cancelled
        a("formula")->trigger();
        check(e->getContents().slice(4), delta("[{'insert':{'video':'https://v'}},{'insert':' '},{'insert':{'formula':'e=mc^2'}},"
                                               "{'insert':'x\\n'}]"), __LINE__);
        QCOMPARE(e->getSelection(), (Range{7, 0}));
        // In the middle of a line both halves keep the line format.
        start("[{'insert':'abcd'},{'insert':'\\n','attributes':{'list':'bullet'}}]", 2, 0);
        a("image")->trigger();
        check(e->getContents(), delta("[{'insert':'ab'},{'insert':'\\n','attributes':{'list':'bullet'}},{'insert':{'image':'file:///tmp/x.png'}},"
                                      "{'insert':'\\ncd'},{'insert':'\\n','attributes':{'list':'bullet'}}]"), __LINE__);
    }

    void historyAndReadOnly()
    {
        start("[{'insert':'ab\\n'}]", 0, 2);
        QVERIFY(!a("undo")->isEnabled());
        a("bold")->trigger();
        QVERIFY(a("undo")->isEnabled());
        a("undo")->trigger();
        check(e->getContents(), delta("[{'insert':'ab\\n'}]"), __LINE__);
        QVERIFY(a("redo")->isEnabled());
        a("redo")->trigger();
        check(e->getContents(), delta("[{'insert':'ab','attributes':{'bold':true}},{'insert':'\\n'}]"), __LINE__);
        e->setReadOnly(true);
        bar->updateState();
        QVERIFY(!a("bold")->isEnabled());
        QVERIFY(!combo("header")->isEnabled());
        QVERIFY(a("search")->isEnabled());
    }

    void findBar()
    {
        start("[{'insert':'one two One\\nthree one\\n'}]");
        a("search")->trigger();
        QVERIFY(find->isVisible());
        find->lineEdit()->setText(QStringLiteral("one"));
        QCOMPARE(find->matchCount(), 3);
        QCOMPARE(e->extraSelections().size(), 3);
        find->findNext();
        QCOMPARE(e->getSelection(), (Range{0, 3}));
        find->findNext();
        QCOMPARE(e->getSelection(), (Range{8, 3}));
        find->findPrevious();
        QCOMPARE(e->getSelection(), (Range{0, 3}));
        find->setCaseSensitive(true);
        QCOMPARE(find->matchCount(), 2);
        // Matches follow edits.
        e->insertText(0, QStringLiteral("one "));
        QCOMPARE(find->matchCount(), 3);
        QTest::keyClick(find->lineEdit(), Qt::Key_Escape);
        QVERIFY(!find->isVisible());
        QVERIFY(!a("search")->isChecked());
        QVERIFY(e->extraSelections().isEmpty());
        QCOMPARE(rec->records.size(), 1); // only the insert; searching changes nothing
    }

    void iconsFollowPalette()
    {
        const QPalette saved = QApplication::palette();
        for (const QColor& text : {QColor(Qt::black), QColor(Qt::white)}) {
            QPalette pal = saved;
            pal.setColor(QPalette::WindowText, text);
            QApplication::setPalette(pal);
            for (const QString& name : detail::toolbarIconNames()) {
                const QImage img = detail::toolbarIcon(name).pixmap(48, 48).toImage();
                bool found = false;
                for (int y = 0; y < img.height() && !found; ++y)
                    for (int x = 0; x < img.width() && !found; ++x)
                        found = img.pixelColor(x, y).alpha() == 255 && img.pixelColor(x, y).rgb() == text.rgb();
                QVERIFY2(found, qPrintable(name));
            }
        }
        QApplication::setPalette(saved);
        bar->grab().save(QStringLiteral(QUILLQT_TEST_OUTPUT_DIR "/toolbar.png"));
    }
};

QTEST_MAIN(TestToolbar)
#include "tst_toolbar.moc"
