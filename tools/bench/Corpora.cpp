#include "Corpora.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>

using quill::AttributeMap;
using quill::Delta;

namespace quillbench {
namespace {

const QStringList kWords{"lorem",  "ipsum", "dolor",   "sit",    "amet",       "consectetur", "adipiscing", "elit",
                         "sed",    "do",    "eiusmod", "tempor", "incididunt", "ut",          "labore",     "et",
                         "dolore", "magna", "aliqua",  "enim",   "minim",      "veniam",      "quis",       "nostrud"};

QString words(QRandomGenerator& rng, int n)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out << kWords.at(int(rng.bounded(qsizetype(kWords.size()))));
    return out.join(QLatin1Char(' '));
}

AttributeMap attrs(std::initializer_list<std::pair<const char*, QJsonValue>> list)
{
    AttributeMap m;
    for (const auto& [k, v] : list)
        m.insert(QString::fromLatin1(k), v);
    return m;
}

/// A 1x1 PNG as a data URL (the image size comes from the attributes).
QString dataImage()
{
    return QStringLiteral(
        "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");
}

void paragraph(Delta& d, QRandomGenerator& rng)
{
    // ~100 characters with a few formatted runs.
    const int runs = 4 + int(rng.bounded(4));
    for (int r = 0; r < runs; ++r) {
        const QString text = words(rng, 2 + int(rng.bounded(4))) + QLatin1Char(' ');
        switch (rng.bounded(8)) {
        case 0:
            d.insert(text, attrs({{"bold", true}}));
            break;
        case 1:
            d.insert(text, attrs({{"italic", true}}));
            break;
        case 2:
            d.insert(text, attrs({{"link", QStringLiteral("https://example.org/%1").arg(rng.bounded(100))}}));
            break;
        case 3:
            d.insert(text, attrs({{"color", QStringLiteral("#e53935")}}));
            break;
        default:
            d.insert(text);
        }
    }
}

Delta mixed(int lines)
{
    QRandomGenerator rng(1);
    Delta d;
    int i = 0;
    while (i < lines) {
        const int kind = i % 50;
        if (kind == 0) {
            d.insert(words(rng, 4));
            d.insert(QStringLiteral("\n"), attrs({{"header", 1 + int(rng.bounded(3))}}));
            ++i;
        } else if (kind == 10) {
            for (int k = 0; k < 5 && i < lines; ++k, ++i) {
                d.insert(words(rng, 6));
                AttributeMap a = attrs({{"list", k % 2 ? "ordered" : "bullet"}});
                if (k > 1)
                    a.insert(QStringLiteral("indent"), 1);
                d.insert(QStringLiteral("\n"), a);
            }
        } else if (kind == 20) {
            for (int k = 0; k < 2 && i < lines; ++k, ++i) {
                d.insert(words(rng, 10));
                d.insert(QStringLiteral("\n"), attrs({{"blockquote", true}}));
            }
        } else if (kind == 30) {
            for (int k = 0; k < 4 && i < lines; ++k, ++i) {
                d.insert(QStringLiteral("    var x%1 = %2;").arg(i).arg(rng.bounded(1000)));
                d.insert(QStringLiteral("\n"), attrs({{"code-block", true}}));
            }
        } else if (i % 200 == 199) {
            d.insert(QJsonObject{{QStringLiteral("image"), dataImage()}},
                     attrs({{"style", QStringLiteral("width: 120; height: 80")}}));
            d.insert(QStringLiteral("\n"));
            ++i;
        } else {
            paragraph(d, rng);
            d.insert(QStringLiteral("\n"));
            ++i;
        }
    }
    return d;
}

Delta plain(int lines)
{
    QRandomGenerator rng(2);
    QString text;
    for (int i = 0; i < lines; ++i)
        text += words(rng, 6) + QLatin1Char('\n');
    return Delta().insert(text);
}

Delta longLine(int chars)
{
    QRandomGenerator rng(3);
    Delta d;
    qsizetype n = 0;
    bool bold = false;
    while (n < chars) {
        QString run = words(rng, 12);
        run.truncate(50);
        run += QLatin1Char(' ');
        d.insert(run, bold ? attrs({{"bold", true}}) : AttributeMap());
        bold = !bold;
        n += run.size();
    }
    d.insert(QStringLiteral("\n"));
    return d;
}

Delta manyEmbeds(int count)
{
    Delta d;
    for (int i = 0; i < count; ++i) {
        if (i % 2 == 0)
            d.insert(QJsonObject{{QStringLiteral("image"), dataImage()}},
                     attrs({{"style", QStringLiteral("width: 64; height: 48")}}));
        else
            d.insert(QJsonObject{{QStringLiteral("video"), QStringLiteral("https://example.org/v%1.mp4").arg(i)}});
        d.insert(QStringLiteral("\nCaption %1\n").arg(i));
    }
    return d;
}

Delta nestedLists(int items)
{
    QRandomGenerator rng(4);
    static const char* types[] = {"bullet", "ordered", "unchecked"};
    Delta d;
    for (int i = 0; i < items; ++i) {
        d.insert(words(rng, 5));
        AttributeMap a = attrs({{"list", types[(i / 7) % 3]}});
        const int indent = int(rng.bounded(9));
        if (indent > 0)
            a.insert(QStringLiteral("indent"), indent);
        d.insert(QStringLiteral("\n"), a);
    }
    return d;
}

Delta flutterSample(int times, const QString& fixturesDir)
{
    QFile f(fixturesDir + QStringLiteral("/flutter_quill/example_default_sample.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot read %s", qPrintable(f.fileName()));
    const auto one = Delta::fromJson(QJsonDocument::fromJson(f.readAll()).array());
    if (!one)
        qFatal("invalid sample");
    Delta d;
    for (int i = 0; i < times; ++i)
        d = d.concat(*one);
    return d;
}

} // namespace

QStringList corpusNames()
{
    return {QStringLiteral("mixed"),       QStringLiteral("plain"),        QStringLiteral("long-line"),
            QStringLiteral("many-embeds"), QStringLiteral("nested-lists"), QStringLiteral("flutter-sample")};
}

Delta corpus(const QString& name, double scale, const QString& fixturesDir)
{
    const auto n = [scale](int base) { return std::max(1, int(base * scale)); };
    if (name == QLatin1String("mixed"))
        return mixed(n(10000));
    if (name == QLatin1String("plain"))
        return plain(n(100000));
    if (name == QLatin1String("long-line"))
        return longLine(n(200000));
    if (name == QLatin1String("many-embeds"))
        return manyEmbeds(n(2000));
    if (name == QLatin1String("nested-lists"))
        return nestedLists(n(5000));
    if (name == QLatin1String("flutter-sample"))
        return flutterSample(n(200), fixturesDir);
    qFatal("unknown corpus %s", qPrintable(name));
}

QString pasteHtml(int lines)
{
    QRandomGenerator rng(5);
    QString html = QStringLiteral("<html><body>");
    for (int i = 0; i < lines; ++i) {
        switch (i % 10) {
        case 0:
            html += QStringLiteral("<h2>%1</h2>").arg(words(rng, 4));
            break;
        case 1:
        case 2:
            html += QStringLiteral("<ul><li>%1</li></ul>").arg(words(rng, 6));
            break;
        default:
            html += QStringLiteral("<p>%1 <b>%2</b> <i>%3</i></p>").arg(words(rng, 5), words(rng, 2), words(rng, 3));
        }
    }
    return html + QStringLiteral("</body></html>");
}

} // namespace quillbench
