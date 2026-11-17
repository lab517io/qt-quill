#include "quill/OtClient.h"

namespace quill {

OtClient::State OtClient::state() const
{
    if (!m_outstanding)
        return State::Synchronized;
    return m_buffer ? State::AwaitingWithBuffer : State::AwaitingConfirm;
}

void OtClient::reset(qint64 revision)
{
    m_revision = revision;
    m_outstanding.reset();
    m_buffer.reset();
}

void OtClient::sendOutstanding()
{
    if (send)
        send(m_revision, *m_outstanding);
}

void OtClient::applyClient(const Delta& change)
{
    if (change.isEmpty())
        return;
    if (!m_outstanding) {
        m_outstanding = change;
        ++m_sequence;
        sendOutstanding();
    } else if (!m_buffer) {
        m_buffer = change;
    } else {
        m_buffer = m_buffer->compose(change);
    }
}

void OtClient::applyServer(const Delta& changeIn)
{
    ++m_revision;
    Delta change = changeIn;
    // The server's change happened first: it wins ties.
    if (m_outstanding) {
        const Delta outstanding = *m_outstanding;
        m_outstanding = change.transform(outstanding, true);
        change = outstanding.transform(change, false);
    }
    if (m_buffer) {
        const Delta buffer = *m_buffer;
        m_buffer = change.transform(buffer, true);
        change = buffer.transform(change, false);
    }
    if (apply && !change.isEmpty())
        apply(change);
}

void OtClient::serverAck()
{
    if (!m_outstanding)
        return; // not ours to count (a stray or duplicated ack)
    ++m_revision;
    m_outstanding = m_buffer;
    m_buffer.reset();
    if (m_outstanding) {
        ++m_sequence;
        sendOutstanding();
    }
}

void OtClient::serverReconnect()
{
    if (m_outstanding)
        sendOutstanding();
}

qsizetype OtClient::transformFromServer(qsizetype index) const
{
    // As applyServer() transforms a change: past the change in flight, then the buffer.
    if (m_outstanding)
        index = m_outstanding->transformPosition(index, false);
    if (m_buffer)
        index = m_buffer->transformPosition(index, false);
    return index;
}

namespace {

/// Maps a position of the document after `change` to the document before it.
qsizetype positionBefore(const Delta& change, qsizetype pos)
{
    qsizetype before = 0; // in the document before the change
    qsizetype after = 0;  // in the document after it
    for (const Op& op : change.ops()) {
        const qsizetype n = op.length();
        if (op.isDelete()) {
            before += n;
        } else if (pos < after + n) {
            // Inside a retain: the same character; inside an insert: where it was inserted.
            return op.isInsert() ? before : before + (pos - after);
        } else {
            after += n;
            if (!op.isInsert())
                before += n;
        }
    }
    return before + (pos - after);
}

} // namespace

qsizetype OtClient::transformToServer(qsizetype index) const
{
    // The local document is the server's plus the change in flight plus
    // the buffer; undo them in reverse order.
    if (m_buffer)
        index = positionBefore(*m_buffer, index);
    if (m_outstanding)
        index = positionBefore(*m_outstanding, index);
    return index;
}

} // namespace quill
