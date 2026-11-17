#pragma once

#include "quill/Delta.h"

#include <QHash>
#include <QList>
#include <QString>

namespace quill {

/// Server side of client-server operational transformation: the document,
/// its revision and the log of applied changes. See OtClient.
///
/// A client sends a change together with the revision it is based on. The
/// server transforms it against the changes applied since (those win ties),
/// applies it and returns it for broadcasting to the other clients; the
/// sender gets an acknowledgement. No I/O.
class OtServer
{
public:
    struct Entry
    {
        Delta change;     ///< as applied
        QString clientId; ///< who sent it
        qint64 sequence = 0;
    };

    struct Result
    {
        enum class Status {
            Applied,   ///< applied as revision `revision`; broadcast `change`
            Duplicate, ///< this (clientId, sequence) was applied before; nothing to do
            Rejected,  ///< invalid; `error` says why, the client must reload
        };
        Status status = Status::Rejected;
        qint64 revision = 0;
        Delta change;
        QString error;
    };

    explicit OtServer(const Delta& document = Delta().insert(QStringLiteral("\n")));

    /// Applies `change`, made by `clientId` on revision `baseRevision`.
    /// `sequence` numbers a client's changes (OtClient::sequence()); a
    /// change with a sequence number not above the last one applied for
    /// that client is a resend and reported as Duplicate. Sequence 0
    /// disables the check.
    ///
    /// Rejected: an unknown revision, a change longer than the document,
    /// or one removing the final newline.
    Result receive(qint64 baseRevision, const Delta& change, const QString& clientId = {}, qint64 sequence = 0);

    const Delta& document() const { return m_document; }
    /// Number of changes applied.
    qint64 revision() const { return m_log.size(); }
    /// The changes after `revision` (empty when it is current or invalid).
    QList<Entry> entriesSince(qint64 revision) const;
    const QList<Entry>& log() const { return m_log; }

private:
    Delta m_document;
    QList<Entry> m_log;
    QHash<QString, qint64> m_lastSequence;
};

} // namespace quill
