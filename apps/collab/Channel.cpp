#include "Channel.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTimer>

namespace collab {

namespace {
qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch();
}
} // namespace

// --- Channel ------------------------------------------------------------------

Channel::Channel(QObject* parent) : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &Channel::flush);
}

void Channel::setLatency(int ms)
{
    if (ms <= 0)
        m_latency = nullptr;
    else
        m_latency = [ms] { return ms; };
}

void Channel::setLatency(std::function<int()> ms)
{
    m_latency = std::move(ms);
}

void Channel::send(const QJsonObject& message)
{
    const int delay = m_latency ? m_latency() : 0;
    if (delay <= 0 && m_pending.empty()) {
        transmit(message);
        return;
    }
    // In order: never before a message queued earlier.
    const qint64 due = std::max(now() + delay, m_pending.empty() ? 0 : m_pending.back().due);
    m_pending.push_back({due, message, true});
    schedule();
}

void Channel::deliver(const QJsonObject& message)
{
    const int delay = m_latency ? m_latency() : 0;
    if (delay <= 0 && m_pending.empty()) {
        Q_EMIT received(message);
        return;
    }
    const qint64 due = std::max(now() + delay, m_pending.empty() ? 0 : m_pending.back().due);
    m_pending.push_back({due, message, false});
    schedule();
}

void Channel::dropPending()
{
    m_pending.clear();
    m_timer->stop();
}

void Channel::schedule()
{
    if (m_pending.empty())
        return;
    m_timer->start(int(std::max<qint64>(0, m_pending.front().due - now())));
}

void Channel::flush()
{
    const QPointer<Channel> self(this);
    while (self && !m_pending.empty() && m_pending.front().due <= now()) {
        const Pending p = m_pending.front();
        m_pending.pop_front();
        if (p.outgoing)
            transmit(p.message);
        else
            Q_EMIT received(p.message);
    }
    if (self)
        schedule();
}

// --- TcpChannel -------------------------------------------------------------------

TcpChannel::TcpChannel(QTcpSocket* socket, QObject* parent) : Channel(parent), m_socket(socket)
{
    socket->setParent(this);
    connect(socket, &QTcpSocket::connected, this, &Channel::opened);
    connect(socket, &QTcpSocket::readyRead, this, &TcpChannel::readLines);
    connect(socket, &QTcpSocket::disconnected, this, [this] {
        dropPending();
        Q_EMIT closed();
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_socket->state() != QAbstractSocket::ConnectedState) {
            dropPending();
            Q_EMIT closed();
        }
    });
}

TcpChannel* TcpChannel::connectTo(const QString& host, quint16 port, QObject* parent)
{
    auto* socket = new QTcpSocket;
    auto* channel = new TcpChannel(socket, parent);
    socket->connectToHost(host, port);
    return channel;
}

bool TcpChannel::isOpen() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void TcpChannel::close()
{
    m_socket->disconnectFromHost();
}

void TcpChannel::transmit(const QJsonObject& message)
{
    if (!isOpen())
        return;
    m_socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void TcpChannel::readLines()
{
    const QPointer<TcpChannel> self(this);
    while (self && m_socket->canReadLine()) {
        const QByteArray line = m_socket->readLine().trimmed();
        if (line.isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning("quill-collab: ignoring invalid message: %s", qPrintable(error.errorString()));
            continue;
        }
        deliver(doc.object());
    }
}

// --- MemoryChannel ------------------------------------------------------------------

std::pair<MemoryChannel*, MemoryChannel*> MemoryChannel::pair(QObject* parent)
{
    auto* a = new MemoryChannel(parent);
    auto* b = new MemoryChannel(parent);
    a->m_peer = b;
    b->m_peer = a;
    QTimer::singleShot(0, a, &Channel::opened);
    QTimer::singleShot(0, b, &Channel::opened);
    return {a, b};
}

MemoryChannel::~MemoryChannel()
{
    if (m_peer) {
        MemoryChannel* peer = m_peer;
        peer->m_peer = nullptr;
        peer->dropPending();
        Q_EMIT peer->closed();
    }
}

void MemoryChannel::close()
{
    if (!m_peer)
        return;
    MemoryChannel* peer = m_peer;
    m_peer = nullptr;
    peer->m_peer = nullptr;
    dropPending();
    peer->dropPending();
    Q_EMIT peer->closed();
    Q_EMIT closed();
}

void MemoryChannel::transmit(const QJsonObject& message)
{
    if (m_peer)
        m_peer->deliver(message);
}

} // namespace collab
