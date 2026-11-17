#include "quill/OtServer.h"

namespace quill {

namespace {

/// Length of the document a change applies to (its retains and deletes).
qsizetype baseLength(const Delta& change)
{
    qsizetype n = 0;
    for (const Op& op : change.ops()) {
        if (!op.isInsert())
            n += op.length();
    }
    return n;
}

bool endsWithNewline(const Delta& document)
{
    if (document.ops().isEmpty())
        return false;
    const Op& last = document.ops().constLast();
    return last.isTextInsert() && last.text().endsWith(QLatin1Char('\n'));
}

} // namespace

OtServer::OtServer(const Delta& document) : m_document(document) {}

OtServer::Result OtServer::receive(qint64 baseRevision, const Delta& changeIn, const QString& clientId, qint64 sequence)
{
    Result result;
    if (baseRevision < 0 || baseRevision > revision()) {
        result.error = QStringLiteral("unknown revision %1 (server is at %2)").arg(baseRevision).arg(revision());
        return result;
    }
    if (sequence > 0 && !clientId.isEmpty() && sequence <= m_lastSequence.value(clientId, 0)) {
        result.status = Result::Status::Duplicate;
        result.revision = revision();
        return result;
    }
    Delta change = changeIn;
    try {
        for (qint64 r = baseRevision; r < revision(); ++r)
            change = m_log.at(r).change.transform(change, true);
        if (baseLength(change) > m_document.length()) {
            result.error = QStringLiteral("change is longer than the document");
            return result;
        }
        const Delta next = m_document.compose(change);
        if (!next.isDocument() || !endsWithNewline(next)) {
            result.error = QStringLiteral("change removes the final newline");
            return result;
        }
        m_document = next;
    } catch (const DeltaError& e) {
        result.error = QString::fromUtf8(e.what());
        return result;
    }
    m_log.append({change, clientId, sequence});
    if (sequence > 0 && !clientId.isEmpty())
        m_lastSequence.insert(clientId, sequence);
    result.status = Result::Status::Applied;
    result.revision = revision();
    result.change = change;
    return result;
}

QList<OtServer::Entry> OtServer::entriesSince(qint64 rev) const
{
    if (rev < 0 || rev >= revision())
        return {};
    return m_log.mid(rev);
}

} // namespace quill
