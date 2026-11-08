// Placeholder demo: a plain QTextEdit. Replaced by the full demo in milestone M6.
#include <quill/Version.h>

#include <QApplication>
#include <QTextEdit>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QTextEdit editor;
    editor.setWindowTitle(QStringLiteral("quill-demo %1 (Qt %2)")
                              .arg(quill::version(), QString::fromLatin1(qVersion())));
    editor.resize(800, 600);
    editor.show();
    return app.exec();
}
