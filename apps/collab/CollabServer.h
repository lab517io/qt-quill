#pragma once

#include "Channel.h"

#include <quill/OtServer.h>

#include <QHash>
#include <QObject>

class QTcpServer;

namespace collab {

/// The quill-collab server: an OtServer and the connected clients. Accepts
/// Channels from any transport (listen() for TCP, addChannel() directly).
class CollabServer : public QObject
{
    Q_OBJECT
public:
    explicit CollabServer(const quill::Delta& document = quill::Delta().insert(QStringLiteral("\n")),
                          QObject* parent = nullptr);

    /// Accepts TCP connections; port 0 picks a free port (see port()).
    bool listen(quint16 port, const QString& address = QString());
    quint16 port() const;

    /// Takes over a connected channel.
    void addChannel(Channel* channel);

    const quill::OtServer& ot() const { return m_ot; }
    int clientCount() const;

Q_SIGNALS:
    void changed(); ///< a change was applied or a client came or went

private:
    struct Client
    {
        QString id;
        QString name;
        QString color;
        int index = 0;
        int length = 0;
        bool hasCursor = false;
        bool joined = false;
    };

    void onMessage(Channel* channel, const QJsonObject& m);
    void onClosed(Channel* channel);
    void broadcast(const QJsonObject& m, Channel* except);
    QJsonArray peers(Channel* except) const;
    static QJsonObject peerJson(const Client& c);

    quill::OtServer m_ot;
    QHash<Channel*, Client> m_clients;
    int m_nextId = 1;
    QTcpServer* m_tcp = nullptr;
};

} // namespace collab
