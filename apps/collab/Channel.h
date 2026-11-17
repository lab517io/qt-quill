#pragma once

// Message transports for quill-collab: line-delimited JSON over TCP, and an
// in-memory pair for tests. Both can delay messages (the latency panel,
// random test latency) while keeping them in order.

#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include <deque>
#include <functional>

class QTcpSocket;
class QTimer;

namespace collab {

/// One end of a connection carrying JSON objects, in order.
class Channel : public QObject
{
    Q_OBJECT
public:
    explicit Channel(QObject* parent = nullptr);

    /// Sends `message` (after the outgoing latency).
    void send(const QJsonObject& message);
    virtual bool isOpen() const = 0;
    /// Closes the connection; both ends get closed().
    virtual void close() = 0;

    /// Delay of each message in both directions, in ms. A function so tests
    /// can draw a random delay per message; order is always kept.
    void setLatency(int ms);
    void setLatency(std::function<int()> ms);

Q_SIGNALS:
    void opened();
    void received(const QJsonObject& message);
    void closed();

protected:
    /// Sends now.
    virtual void transmit(const QJsonObject& message) = 0;
    /// The transport received a message (it is delivered after the latency).
    void deliver(const QJsonObject& message);
    /// Drops messages still waiting for their latency (connection lost).
    void dropPending();

private:
    struct Pending
    {
        qint64 due;
        QJsonObject message;
        bool outgoing;
    };
    void schedule();
    void flush();

    std::function<int()> m_latency;
    std::deque<Pending> m_pending;
    QTimer* m_timer;
};

/// A Channel over a TCP socket: one compact JSON object per line.
class TcpChannel : public Channel
{
    Q_OBJECT
public:
    /// Takes over a connected socket (server side) or one about to connect.
    explicit TcpChannel(QTcpSocket* socket, QObject* parent = nullptr);
    /// Client side: connects to host:port; opened() when connected.
    static TcpChannel* connectTo(const QString& host, quint16 port, QObject* parent = nullptr);

    bool isOpen() const override;
    void close() override;

protected:
    void transmit(const QJsonObject& message) override;

private:
    void readLines();

    QTcpSocket* m_socket;
};

/// Two connected in-memory channels (tests). Delete either end to close.
class MemoryChannel : public Channel
{
    Q_OBJECT
public:
    static std::pair<MemoryChannel*, MemoryChannel*> pair(QObject* parent = nullptr);
    ~MemoryChannel() override;

    bool isOpen() const override { return m_peer; }
    void close() override;

protected:
    void transmit(const QJsonObject& message) override;

private:
    using Channel::Channel;
    QPointer<MemoryChannel> m_peer;
};

} // namespace collab
