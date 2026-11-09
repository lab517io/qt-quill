// quill-demo: QuillEditor with toolbar, Delta inspector and file I/O.
//
//   quill-demo [document.json] [--sample name.json] [--dark] [--log]
//              [--screenshot out.png]
//
// --screenshot renders the window into a PNG and exits (ci/screenshot.sh).
// --log prints every change Delta to stdout.
#include "MainWindow.h"

#include <quill/EmbedRegistry.h>
#include <quill/QuillEditor.h>

#include <quill/QuillEditor.h>
#include <quill/Version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("quill-demo"));
    QApplication::setApplicationVersion(quill::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Quill Delta editor demo"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Delta JSON document to open."));
    const QCommandLineOption sample(QStringLiteral("sample"), QStringLiteral("Load an embedded sample."),
                                    QStringLiteral("name"));
    const QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Save a screenshot and exit."),
                                        QStringLiteral("png"));
    const QCommandLineOption dark(QStringLiteral("dark"), QStringLiteral("Dark palette."));
    const QCommandLineOption log(QStringLiteral("log"), QStringLiteral("Print change Deltas."));
    const QCommandLineOption offline(QStringLiteral("offline"),
                                     QStringLiteral("Do not load remote images (deterministic screenshots)."));
    parser.addOptions({sample, screenshot, dark, log, offline});
    parser.process(app);

    MainWindow window;
    if (parser.isSet(offline))
        window.editor()->embeds()->setRemoteImagesEnabled(false);
    if (parser.isSet(dark))
        window.setDarkPalette(true);
    if (parser.isSet(sample) && !window.loadSample(QStringLiteral(":/samples/") + parser.value(sample))) {
        QTextStream(stderr) << "no sample " << parser.value(sample) << '\n';
        return 1;
    }
    if (!parser.positionalArguments().isEmpty()) {
        QString error;
        if (!window.openFile(parser.positionalArguments().first(), &error)) {
            QTextStream(stderr) << error << '\n';
            return 1;
        }
    }
    if (parser.isSet(log)) {
        QObject::connect(window.editor(), &quill::QuillEditor::textChange,
                         [](const quill::Delta& change, const quill::Delta&, quill::Source source) {
                             QTextStream(stdout)
                                 << (source == quill::Source::User ? "user " : "api  ") << change.toJson() << Qt::endl;
                         });
    }
    window.show();
    if (parser.isSet(screenshot)) {
        const QString out = parser.value(screenshot);
        QTimer::singleShot(300, &window, [&window, out] {
            QDir().mkpath(QFileInfo(out).absolutePath());
            const bool ok = window.grab().save(out);
            QTextStream(ok ? stdout : stderr) << (ok ? "wrote " : "cannot write ") << out << '\n';
            QApplication::exit(ok ? 0 : 1);
        });
    }
    return app.exec();
}
