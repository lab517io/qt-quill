#pragma once

#include "Channel.h"

#include <quill/OtClient.h>
#include <quill/Range.h>
#include <quill/Source.h>

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>

class QTimer;

namespace quill {
class QuillEditor;
}

namespace collab {

/// Connects a QuillEditor to a CollabServer: the editor glue a host needs
/// for collaborative editing.
///
/// - Source::User changes (typing, toolbar, undo) go to the OtClient and on
///   to the server. Remote changes are applied with Source::Api, so they are
///   not sent back, and the editor's History (installed with userOnly)
///   transforms its steps instead of recording them.
/// - The local cursor is sent (at most 20 times a second); peers' cursors
///   are shown with the editor's RemoteCursors.
/// - When the connection drops, editing goes on (changes are buffered); a
///   new channel given to reconnect() resumes where it left off.
class CollabSession : public QObject
{
    Q_OBJECT
public:
    CollabSession(quill::QuillEditor* editor, const QString& name, const QColor& color, QObject* parent = nullptr);

    /// Starts on `channel` (taken over); sends hello once it is open.
    void connectTo(Channel* channel);
    /// Continues on a new channel after the old one closed.
    void reconnect(Channel* channel);
    /// Closes the channel (going offline); edits are kept for later.
    void disconnect();

    bool isConnected() const { return m_joined; }
    QString clientId() const { return m_clientId; }
    QString name() const { return m_name; }
    QColor color() const { return m_color; }
    const quill::OtClient& client() const { return m_client; }
    int peerCount() const { return int(m_peers.size()); }
    /// Remote changes the editor applied differently from the server
    /// (normalised); the difference was sent back as a local change.
    int corrections() const { return m_corrections; }
    /// The server rejected a change; the session reloaded the document.
    int errors() const { return m_errors; }

Q_SIGNALS:
    void statusChanged();
    /// The channel closed (the host may call reconnect()).
    void disconnected();

private:
    struct Peer
    {
        QString name;
        QColor color;
    };

    void attach(Channel* channel);
    void sendHello();
    void onMessage(const QJsonObject& m);
    void onLocalChange(const quill::Delta& change, quill::Source source);
    void applyRemote(const quill::Delta& change);
    void addPeers(const QJsonArray& peers);
    void showCursor(const QString& id, qsizetype index, qsizetype length);
    void scheduleCursor();
    void sendCursor();

    QPointer<quill::QuillEditor> m_editor;
    QString m_name;
    QColor m_color;
    QPointer<Channel> m_channel;
    quill::OtClient m_client;
    QString m_clientId;
    bool m_joined = false;
    bool m_applyingRemote = false;
    QHash<QString, Peer> m_peers;
    QTimer* m_cursorTimer;
    qint64 m_lastCursorSent = 0;
    quill::Range m_sentCursor{-1, 0};
    bool m_cursorPending = false;
    int m_corrections = 0;
    int m_errors = 0;
};

} // namespace collab
