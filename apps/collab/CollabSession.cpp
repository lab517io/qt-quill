#include "CollabSession.h"

#include "Protocol.h"

#include <quill/QuillEditor.h>
#include <quill/RemoteCursors.h>

#include <QDateTime>
#include <QTimer>

namespace collab {

using quill::Delta;
using quill::Source;

namespace {
constexpr int kCursorInterval = 50; // ms: at most 20 cursor messages a second
}

CollabSession::CollabSession(quill::QuillEditor* editor, const QString& name, const QColor& color, QObject* parent)
    : QObject(parent), m_editor(editor), m_name(name), m_color(color), m_cursorTimer(new QTimer(this))
{
    m_cursorTimer->setSingleShot(true);
    connect(m_cursorTimer, &QTimer::timeout, this, &CollabSession::sendCursor);
    quill::HistoryOptions history;
    history.userOnly = true; // undo only the local user's changes
    editor->installHistory(history);
    editor->remoteCursors();

    m_client.send = [this](qint64 rev, const Delta& change) {
        if (!m_channel || !m_joined)
            return; // resent by OtClient::serverReconnect()
        QJsonObject op = proto::make("op");
        op.insert("rev", rev);
        op.insert("seq", m_client.sequence());
        op.insert("delta", change.toJsonArray());
        m_channel->send(op);
    };
    m_client.apply = [this](const Delta& change) { applyRemote(change); };

    connect(editor, &quill::QuillEditor::textChange, this,
            [this](const Delta& change, const Delta&, Source source) { onLocalChange(change, source); });
    connect(editor, &quill::QuillEditor::selectionChange, this, [this] { scheduleCursor(); });
}

void CollabSession::connectTo(Channel* channel)
{
    m_clientId.clear();
    attach(channel);
}

void CollabSession::reconnect(Channel* channel)
{
    attach(channel);
}

void CollabSession::disconnect()
{
    if (m_channel)
        m_channel->close();
}

void CollabSession::attach(Channel* channel)
{
    if (m_channel && m_channel != channel) {
        // Nothing more from the old connection (a late "resume" on it would
        // replay the missed changes a second time).
        Channel* old = m_channel;
        old->disconnect(this);
        old->close();
        old->deleteLater();
    }
    m_channel = channel;
    m_joined = false;
    channel->setParent(this);
    connect(channel, &Channel::received, this, &CollabSession::onMessage);
    connect(channel, &Channel::closed, this, [this, channel] {
        if (channel != m_channel)
            return;
        m_joined = false;
        m_peers.clear();
        if (m_editor)
            m_editor->remoteCursors()->clear();
        Q_EMIT statusChanged();
        Q_EMIT disconnected();
    });
    if (channel->isOpen())
        sendHello();
    else
        connect(channel, &Channel::opened, this, &CollabSession::sendHello);
    Q_EMIT statusChanged();
}

void CollabSession::sendHello()
{
    QJsonObject hello = proto::make("hello");
    hello.insert("name", m_name);
    hello.insert("color", m_color.name());
    if (!m_clientId.isEmpty()) {
        hello.insert("clientId", m_clientId);
        hello.insert("rev", m_client.revision());
    }
    m_channel->send(hello);
}

void CollabSession::onMessage(const QJsonObject& m)
{
    if (!m_editor)
        return;
    const QString type = proto::type(m);
    if (type == QLatin1String("init")) {
        // A fresh start: the server's document replaces the local one
        // (edits made while it was unreachable are lost).
        const auto doc = proto::delta(m.value("doc"));
        m_clientId = m.value("clientId").toString();
        m_client.reset(m.value("rev").toInteger());
        m_applyingRemote = true;
        m_editor->setContents(doc ? *doc : Delta().insert(QStringLiteral("\n")), Source::Silent);
        m_applyingRemote = false;
        m_editor->setSelection(0, 0, Source::Silent);
        m_peers.clear();
        m_editor->remoteCursors()->clear();
        m_joined = true;
        addPeers(m.value("peers").toArray());
        m_sentCursor = {-1, 0};
        scheduleCursor();
    } else if (type == QLatin1String("resume")) {
        m_joined = true;
        for (const QJsonValue& v : m.value("ops").toArray()) {
            const QJsonObject e = v.toObject();
            if (e.value("clientId").toString() == m_clientId && m_client.outstanding()
                && e.value("seq").toInteger() == m_client.sequence()) {
                m_client.serverAck(); // ours, applied before the connection dropped
            } else if (const auto change = proto::delta(e.value("delta"))) {
                m_client.applyServer(*change);
            }
        }
        m_peers.clear();
        m_editor->remoteCursors()->clear();
        addPeers(m.value("peers").toArray());
        m_client.serverReconnect();
        m_sentCursor = {-1, 0};
        scheduleCursor();
    } else if (type == QLatin1String("ack")) {
        m_client.serverAck();
        if (m_cursorPending)
            scheduleCursor();
    } else if (type == QLatin1String("op")) {
        if (const auto change = proto::delta(m.value("delta")))
            m_client.applyServer(*change);
    } else if (type == QLatin1String("cursor")) {
        showCursor(m.value("clientId").toString(), m.value("index").toInteger(), m.value("length").toInteger());
    } else if (type == QLatin1String("join")) {
        addPeers(QJsonArray{m});
    } else if (type == QLatin1String("leave")) {
        const QString id = m.value("clientId").toString();
        m_peers.remove(id);
        m_editor->remoteCursors()->removeCursor(id);
    } else if (type == QLatin1String("error")) {
        // The server and this client disagree; start over from the server's document.
        ++m_errors;
        qWarning("quill-collab: server error: %s", qPrintable(m.value("message").toString()));
        m_clientId.clear();
        m_client.reset(m_client.revision());
        sendHello();
    }
    Q_EMIT statusChanged();
}

void CollabSession::onLocalChange(const Delta& change, Source source)
{
    if (m_applyingRemote || source != Source::User)
        return;
    m_client.applyClient(change);
    Q_EMIT statusChanged();
}

void CollabSession::applyRemote(const Delta& change)
{
    const Delta expected = m_editor->getContents().compose(change);
    const quill::Range before = m_editor->getSelection();
    m_applyingRemote = true;
    m_editor->updateContents(change, Source::Api);
    m_applyingRemote = false;
    // The editor may normalise what it cannot represent; send the
    // difference, so that the server and the peers follow.
    const Delta actual = m_editor->getContents();
    if (actual != expected) {
        ++m_corrections;
        m_client.applyClient(expected.diff(actual));
    }
    // The editor keeps the caret before text inserted at it (Quill), the
    // peers' RemoteCursors move it after; tell them where it really is.
    if (m_editor->getSelection() != before)
        scheduleCursor();
}

void CollabSession::addPeers(const QJsonArray& peers)
{
    for (const QJsonValue& v : peers) {
        const QJsonObject p = v.toObject();
        const QString id = p.value("clientId").toString();
        if (id.isEmpty() || id == m_clientId)
            continue;
        m_peers.insert(id, {p.value("name").toString(), QColor(p.value("color").toString())});
        if (p.contains("index"))
            showCursor(id, p.value("index").toInteger(), p.value("length").toInteger());
    }
}

void CollabSession::showCursor(const QString& id, qsizetype index, qsizetype length)
{
    const auto peer = m_peers.constFind(id);
    if (peer == m_peers.constEnd() || !m_editor)
        return;
    // Server coordinates -> the local document (past pending local changes).
    const int start = int(m_client.transformFromServer(index));
    const int end = int(m_client.transformFromServer(index + length));
    m_editor->remoteCursors()->setCursor(id, peer->name, peer->color, {start, std::max(0, end - start)});
}

void CollabSession::scheduleCursor()
{
    if (!m_joined || m_cursorTimer->isActive())
        return;
    const qint64 since = QDateTime::currentMSecsSinceEpoch() - m_lastCursorSent;
    m_cursorTimer->start(int(std::clamp<qint64>(kCursorInterval - since, 0, kCursorInterval)));
}

void CollabSession::sendCursor()
{
    if (!m_joined || !m_channel || !m_editor)
        return;
    // With local changes in flight, the cursor has no exact place in the
    // server's document (inside a pending insert it would land after it).
    // Peers move it along with the changes meanwhile; send it once they
    // are acknowledged.
    if (m_client.state() != quill::OtClient::State::Synchronized) {
        m_cursorPending = true;
        return;
    }
    m_cursorPending = false;
    const quill::Range r = m_editor->getSelection();
    const int start = int(m_client.transformToServer(r.index));
    const int end = int(m_client.transformToServer(r.index + r.length));
    const quill::Range server{start, std::max(0, end - start)};
    if (server == m_sentCursor)
        return;
    m_sentCursor = server;
    m_lastCursorSent = QDateTime::currentMSecsSinceEpoch();
    QJsonObject cursor = proto::make("cursor");
    cursor.insert("rev", m_client.revision());
    cursor.insert("index", server.index);
    cursor.insert("length", server.length);
    m_channel->send(cursor);
}

} // namespace collab
