// Minimal demo: a QuillEditor, optionally loading a Delta JSON file.
// The full demo with a toolbar is milestone M6.
//
//   quill-demo [document.json] [--log]
//
// --log prints every change Delta and selection change to stdout.
#include <quill/QuillEditor.h>
#include <quill/Version.h>

#include <QApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    quill::QuillEditor editor;
    editor.setWindowTitle(QStringLiteral("quill-demo %1 (Qt %2)")
                              .arg(quill::version(), QString::fromLatin1(qVersion())));
    editor.resize(800, 600);

    const QStringList args = app.arguments().mid(1);
    for (const QString& arg : args) {
        if (arg.startsWith(QLatin1String("--")))
            continue;
        QFile file(arg);
        QString error;
        const auto delta = file.open(QIODevice::ReadOnly) ? quill::Delta::fromJson(file.readAll(), &error)
                                                          : std::nullopt;
        if (!delta) {
            QTextStream(stderr) << "cannot load " << arg << ": " << (error.isEmpty() ? file.errorString() : error)
                                << '\n';
            return 1;
        }
        editor.setContents(*delta, quill::Source::Silent);
    }
    if (args.contains(QStringLiteral("--log"))) {
        QObject::connect(&editor, &quill::QuillEditor::textChange,
                         [](const quill::Delta& change, const quill::Delta&, quill::Source source) {
                             QTextStream(stdout) << (source == quill::Source::User ? "user " : "api  ")
                                                 << change.toJson() << Qt::endl;
                         });
        QObject::connect(&editor, &quill::QuillEditor::selectionChange,
                         [](quill::Range range, quill::Range, quill::Source) {
                             QTextStream(stdout) << "selection " << range.index << '+' << range.length << Qt::endl;
                         });
    }
    QObject::connect(&editor, &quill::QuillEditor::linkActivated,
                     [](const QUrl& url) { QTextStream(stdout) << "link " << url.toString() << Qt::endl; });
    editor.show();
    return app.exec();
}
