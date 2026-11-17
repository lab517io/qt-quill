// quill-collab: real-time collaborative editing with QuillEditor, OtClient,
// OtServer, History and RemoteCursors.
//
//   quill-collab --serve 7777 [--doc start.json]       headless server
//   quill-collab --connect host:7777 --name Ann         client window
//   quill-collab --local 3 [--doc start.json]           server + 3 windows
//   quill-collab --local 2 --screenshot out.png         smoke test: scripted
//        edits in every window over loopback TCP, checks that all converge,
//        saves the windows side by side and exits (1 if they diverged)
#include "Channel.h"
#include "CollabServer.h"
#include "CollabSession.h"
#include "CollabWindow.h"

#include <quill/QuillEditor.h>
#include <quill/RemoteCursors.h>
#include <quill/Version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QScreen>
#include <QTextStream>
#include <QTimer>

#include <memory>

using namespace collab;

namespace {

const QStringList kNames{"Ann", "Bob", "Cleo", "Dan", "Eve", "Finn"};
const QStringList kColors{"#e91e63", "#3f51b5", "#009688", "#ff9800", "#9c27b0", "#795548"};

std::optional<quill::Delta> readDoc(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto d = quill::Delta::fromJson(f.readAll());
    return d && d->isDocument() ? d : std::nullopt;
}

/// The smoke test: everyone types and formats at once, then the windows
/// must show the same document.
void script(const std::vector<std::unique_ptr<CollabWindow>>& windows, const QString& out, CollabServer* server)
{
    for (size_t i = 0; i < windows.size(); ++i) {
        quill::QuillEditor* e = windows[i]->editor();
        QTimer::singleShot(300 + int(i) * 40, e, [e, i] {
            const QString name = kNames.at(int(i % kNames.size()));
            e->setSelection(0, 0, quill::Source::User);
            e->updateContents(quill::Delta().insert(name + QStringLiteral(" was here. ")), quill::Source::User);
            e->formatText(0, int(name.size()), {{"bold", true}}, quill::Source::User);
            e->insertText(e->getLength() - 1, QStringLiteral("\nLine from ") + name, {}, quill::Source::User);
            e->setSelection(int(i) * 3, 2, quill::Source::User);
        });
    }
    QTimer::singleShot(1500, qApp, [&windows, out, server] {
        bool same = true;
        const quill::Delta doc = server->ot().document();
        for (const auto& w : windows)
            same = same && w->editor()->getContents() == doc && w->session()->isConnected()
                   && w->session()->client().state() == quill::OtClient::State::Synchronized;
        // Every window shows every peer's selection where that peer has it.
        bool cursors = true;
        for (const auto& w : windows) {
            for (const auto& peer : windows) {
                if (peer == w)
                    continue;
                const auto c = w->editor()->remoteCursors()->cursor(peer->session()->clientId());
                const quill::Range want = peer->editor()->getSelection();
                if (!c || c->range != want) {
                    cursors = false;
                    QTextStream(stderr) << w->session()->name() << " shows " << peer->session()->name() << " at "
                                        << (c ? QString::number(c->range.index) + "+" + QString::number(c->range.length)
                                              : QStringLiteral("nothing"))
                                        << ", expected " << want.index << "+" << want.length << '\n';
                }
            }
        }
        same = same && cursors;
        QSize size(0, 0);
        for (const auto& w : windows)
            size = QSize(size.width() + w->width(), std::max(size.height(), w->height()));
        QImage image(size, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        int x = 0;
        for (const auto& w : windows) {
            p.drawPixmap(x, 0, w->grab());
            x += w->width();
        }
        p.end();
        QDir().mkpath(QFileInfo(out).absolutePath());
        const bool saved = image.save(out);
        QTextStream(same && saved ? stdout : stderr) << (same ? "converged: " : "DIVERGED: ") << doc.toJson() << '\n'
                                                     << (saved ? "wrote " : "cannot write ") << out << '\n';
        QApplication::exit(same && saved ? 0 : 1);
    });
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("quill-collab"));
    QApplication::setApplicationVersion(quill::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Collaborative Quill Delta editing example"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption serve("serve", "Run a server on <port>.", "port");
    const QCommandLineOption connectTo("connect", "Connect to a server at <host:port>.", "host:port");
    const QCommandLineOption local("local", "Start a server and <n> client windows.", "n");
    const QCommandLineOption doc("doc", "Start document of the server (Delta JSON).", "file");
    const QCommandLineOption name("name", "Your name.", "name");
    const QCommandLineOption color("color", "Your colour.", "color");
    const QCommandLineOption latency("latency", "Artificial latency of the clients.", "ms");
    const QCommandLineOption screenshot("screenshot", "With --local: scripted edits, screenshot, exit.", "png");
    parser.addOptions({serve, connectTo, local, doc, name, color, latency, screenshot});
    parser.process(app);

    quill::Delta start = quill::Delta().insert(QStringLiteral("Edit me together.\n"));
    if (parser.isSet(doc)) {
        const auto d = readDoc(parser.value(doc));
        if (!d) {
            QTextStream(stderr) << "cannot read a Delta document from " << parser.value(doc) << '\n';
            return 1;
        }
        start = *d;
    }

    if (parser.isSet(serve)) {
        auto* server = new CollabServer(start, &app);
        if (!server->listen(quint16(parser.value(serve).toUInt()))) {
            QTextStream(stderr) << "cannot listen on port " << parser.value(serve) << '\n';
            return 1;
        }
        QTextStream(stdout) << "quill-collab server on port " << server->port() << Qt::endl;
        QObject::connect(server, &CollabServer::changed, server, [server] {
            QTextStream(stdout) << "revision " << server->ot().revision() << ", " << server->clientCount()
                                << " client(s)" << Qt::endl;
        });
        return app.exec();
    }

    if (parser.isSet(connectTo)) {
        const QString target = parser.value(connectTo);
        const QString host = target.section(':', 0, -2);
        const quint16 port = quint16(target.section(':', -1).toUInt());
        if (host.isEmpty() || port == 0) {
            QTextStream(stderr) << "expected --connect host:port\n";
            return 1;
        }
        CollabWindow window(parser.value(name).isEmpty() ? qEnvironmentVariable("USER", "Guest") : parser.value(name),
                            QColor(parser.value(color).isEmpty() ? kColors.first() : parser.value(color)),
                            [host, port] { return TcpChannel::connectTo(host, port); });
        window.setLatency(parser.value(latency).toInt());
        window.show();
        return app.exec();
    }

    if (parser.isSet(local)) {
        const int n = std::clamp(parser.value(local).toInt(), 1, int(kNames.size()));
        CollabServer server(start);
        if (!server.listen(0, QStringLiteral("127.0.0.1"))) {
            QTextStream(stderr) << "cannot listen on the loopback interface\n";
            return 1;
        }
        const quint16 port = server.port();
        std::vector<std::unique_ptr<CollabWindow>> windows;
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        for (int i = 0; i < n; ++i) {
            auto w = std::make_unique<CollabWindow>(kNames.at(i), QColor(kColors.at(i)), [port] {
                return TcpChannel::connectTo(QStringLiteral("127.0.0.1"), port);
            });
            w->setLatency(parser.value(latency).toInt());
            const int width = std::max(420, screen.width() / n);
            w->resize(width, std::min(560, screen.height()));
            w->move(screen.left() + i * width, screen.top());
            w->show();
            windows.push_back(std::move(w));
        }
        if (parser.isSet(screenshot))
            script(windows, parser.value(screenshot), &server);
        return app.exec();
    }

    parser.showHelp(1);
}
