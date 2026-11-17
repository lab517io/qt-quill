#include "CollabServer.h"

#include "Protocol.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

namespace collab {

using quill::Delta;
using quill::OtServer;

CollabServer::CollabServer(const Delta& document, QObject* parent) : QObject(parent), m_ot(document) {}

bool CollabServer::listen(quint16 port, const QString& address)
{
    if (!m_tcp) {
        m_tcp = new QTcpServer(this);
        connect(m_tcp, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_tcp->nextPendingConnection())
                addChannel(new TcpChannel(socket, this));
        });
    }
    return m_tcp->listen(address.isEmpty() ? QHostAddress::Any : QHostAddress(address), port);
}

quint16 CollabServer::port() const
{
    return m_tcp ? m_tcp->serverPort() : 0;
}

int CollabServer::clientCount() const
{
    int n = 0;
    for (const Client& c : m_clients)
        n += c.joined ? 1 : 0;
    return n;
}

void CollabServer::addChannel(Channel* channel)
{
    channel->setParent(this);
    m_clients.insert(channel, Client());
    connect(channel, &Channel::received, this, [this, channel](const QJsonObject& m) { onMessage(channel, m); });
    connect(channel, &Channel::closed, this, [this, channel] { onClosed(channel); });
}

QJsonObject CollabServer::peerJson(const Client& c)
{
    QJsonObject o{{"clientId", c.id}, {"name", c.name}, {"color", c.color}};
    if (c.hasCursor) {
        o.insert("index", c.index);
        o.insert("length", c.length);
    }
    return o;
}

QJsonArray CollabServer::peers(Channel* except) const
{
    QJsonArray out;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.key() != except && it.value().joined)
            out.append(peerJson(it.value()));
    }
    return out;
}

void CollabServer::broadcast(const QJsonObject& m, Channel* except)
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.key() != except && it.value().joined)
            it.key()->send(m);
    }
}

void CollabServer::onMessage(Channel* channel, const QJsonObject& m)
{
    Client& client = m_clients[channel];
    const QString type = proto::type(m);
    if (type == QLatin1String("hello")) {
        client.name = m.value("name").toString();
        client.color = m.value("color").toString();
        const QString id = m.value("clientId").toString();
        const qint64 rev = m.value("rev").toInteger(-1);
        // A reconnect: replay what the client missed since its revision.
        if (!id.isEmpty() && rev >= 0 && rev <= m_ot.revision()) {
            client.id = id;
            QJsonArray ops;
            for (const OtServer::Entry& e : m_ot.entriesSince(rev))
                ops.append(
                    QJsonObject{{"delta", e.change.toJsonArray()}, {"clientId", e.clientId}, {"seq", e.sequence}});
            QJsonObject resume = proto::make("resume");
            resume.insert("rev", m_ot.revision());
            resume.insert("ops", ops);
            resume.insert("peers", peers(channel));
            channel->send(resume);
        } else {
            client.id = id.isEmpty() ? QStringLiteral("c%1").arg(m_nextId++) : id;
            QJsonObject init = proto::make("init");
            init.insert("rev", m_ot.revision());
            init.insert("doc", m_ot.document().toJsonArray());
            init.insert("clientId", client.id);
            init.insert("peers", peers(channel));
            channel->send(init);
        }
        // Another connection of the same client (it reconnected before the
        // server noticed the old one drop) is gone.
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            if (it.key() != channel && it.value().id == client.id)
                it.value().joined = false;
        }
        client.joined = true;
        QJsonObject join = proto::make("join");
        join.insert("clientId", client.id);
        join.insert("name", client.name);
        join.insert("color", client.color);
        broadcast(join, channel);
        Q_EMIT changed();
        return;
    }
    if (!client.joined)
        return;
    if (type == QLatin1String("op")) {
        const auto change = proto::delta(m.value("delta"));
        OtServer::Result r;
        if (change)
            r = m_ot.receive(m.value("rev").toInteger(-1), *change, client.id, m.value("seq").toInteger());
        else
            r.error = QStringLiteral("invalid delta");
        if (r.status == OtServer::Result::Status::Rejected) {
            QJsonObject error = proto::make("error");
            error.insert("message", r.error);
            channel->send(error);
            return;
        }
        if (r.status == OtServer::Result::Status::Duplicate)
            return;
        // Cursors move with the change.
        for (Client& c : m_clients) {
            if (!c.hasCursor)
                continue;
            const int start = int(r.change.transformPosition(c.index));
            const int end = int(r.change.transformPosition(c.index + c.length));
            c.index = start;
            c.length = std::max(0, end - start);
        }
        QJsonObject ack = proto::make("ack");
        ack.insert("rev", r.revision);
        channel->send(ack);
        QJsonObject op = proto::make("op");
        op.insert("rev", r.revision);
        op.insert("delta", r.change.toJsonArray());
        op.insert("clientId", client.id);
        broadcast(op, channel);
        Q_EMIT changed();
    } else if (type == QLatin1String("cursor")) {
        const qint64 rev = m.value("rev").toInteger(-1);
        if (rev < 0 || rev > m_ot.revision())
            return;
        qsizetype start = m.value("index").toInteger();
        qsizetype end = start + m.value("length").toInteger();
        for (const OtServer::Entry& e : m_ot.entriesSince(rev)) {
            start = e.change.transformPosition(start);
            end = e.change.transformPosition(end);
        }
        const qsizetype last = m_ot.document().length() - 1;
        client.index = int(std::clamp<qsizetype>(start, 0, last));
        client.length = int(std::clamp<qsizetype>(end - start, 0, last - client.index));
        client.hasCursor = true;
        QJsonObject cursor = proto::make("cursor");
        cursor.insert("clientId", client.id);
        cursor.insert("index", client.index);
        cursor.insert("length", client.length);
        broadcast(cursor, channel);
    }
}

void CollabServer::onClosed(Channel* channel)
{
    const Client client = m_clients.take(channel);
    channel->deleteLater();
    if (!client.joined)
        return;
    // Still connected through a newer channel?
    for (const Client& c : m_clients) {
        if (c.id == client.id && c.joined)
            return;
    }
    QJsonObject leave = proto::make("leave");
    leave.insert("clientId", client.id);
    broadcast(leave, nullptr);
    Q_EMIT changed();
}

} // namespace collab
