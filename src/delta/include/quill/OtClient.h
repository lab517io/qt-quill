#pragma once

#include "quill/Delta.h"

#include <QtGlobal>

#include <functional>
#include <optional>

namespace quill {

/// Client side of client-server operational transformation (the model of
/// ot.js, ShareDB and Google Wave), for collaborative editing of a Delta.
///
/// The client has at most one change in flight to the server. Local changes
/// made while it waits for the server's acknowledgement are composed into a
/// buffer and sent once the acknowledgement arrives. Changes from the server
/// are transformed against the change in flight and the buffer before they
/// reach the local document, and those two are transformed in return.
///
/// The server's changes win ties (two inserts at the same index: the
/// server's goes first), the same rule OtServer applies, so every client
/// converges to the server's document.
///
/// No I/O: the transport calls the apply*() / server*() methods and installs
/// `send` and `apply`.
class OtClient
{
public:
    enum class State {
        Synchronized,       ///< nothing in flight
        AwaitingConfirm,    ///< one change in flight
        AwaitingWithBuffer, ///< one change in flight, more local changes buffered
    };

    explicit OtClient(qint64 revision = 0) : m_revision(revision) {}

    /// Called with a change to send to the server and the revision it is based on.
    std::function<void(qint64 revision, const Delta& change)> send;
    /// Called with a server change, transformed, to apply to the local document.
    std::function<void(const Delta& change)> apply;

    /// The user changed the local document.
    void applyClient(const Delta& change);
    /// The server sent another client's change (the next revision).
    void applyServer(const Delta& change);
    /// The server accepted the change in flight (the next revision).
    void serverAck();
    /// After a reconnect, once the changes missed while disconnected were
    /// passed to applyServer(): sends the change in flight again. The server
    /// recognises a change it already applied by the sender's id and
    /// sequence number (see OtServer::receive), so resending is safe.
    void serverReconnect();

    /// Moves an index of the server's document (e.g. a peer's cursor
    /// received from the server) to the local document.
    qsizetype transformFromServer(qsizetype index) const;
    /// Moves an index of the local document to the server's revision()
    /// (e.g. the local cursor, before sending it).
    qsizetype transformToServer(qsizetype index) const;

    State state() const;
    /// The last server revision this client knows.
    qint64 revision() const { return m_revision; }
    /// Sequence number of the change in flight (counts sent changes from 1).
    qint64 sequence() const { return m_sequence; }
    const std::optional<Delta>& outstanding() const { return m_outstanding; }
    const std::optional<Delta>& buffer() const { return m_buffer; }
    /// Starts over at `revision` with nothing pending (after loading the
    /// server's document).
    void reset(qint64 revision);

private:
    void sendOutstanding();

    qint64 m_revision = 0;
    qint64 m_sequence = 0;
    std::optional<Delta> m_outstanding;
    std::optional<Delta> m_buffer;
};

} // namespace quill
