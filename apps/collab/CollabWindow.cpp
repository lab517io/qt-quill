#include "CollabWindow.h"

#include "../demo/DeltaInspector.h"
#include "Channel.h"
#include "CollabSession.h"

#include <quill/QuillEditor.h>
#include <quill/QuillToolbar.h>

#include <QCheckBox>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace collab {

CollabWindow::CollabWindow(const QString& name, const QColor& color, std::function<Channel*()> connect, QWidget* parent)
    : QMainWindow(parent), m_editor(new quill::QuillEditor(this)), m_connect(std::move(connect))
{
    setWindowTitle(tr("quill-collab - %1").arg(name));
    setCentralWidget(m_editor);
    addToolBar(new quill::QuillToolbar(m_editor, this));

    // Debug bar: latency and offline, to show concurrent edits by hand.
    auto* debug = new QToolBar(tr("Network"), this);
    debug->setObjectName(QStringLiteral("network"));
    debug->addWidget(new QLabel(tr(" Latency "), debug));
    m_latency = new QSpinBox(debug);
    m_latency->setRange(0, 5000);
    m_latency->setSingleStep(100);
    m_latency->setSuffix(tr(" ms"));
    debug->addWidget(m_latency);
    m_offline = new QCheckBox(tr("Offline"), debug);
    debug->addWidget(m_offline);
    auto* inspect = new QCheckBox(tr("Delta"), debug);
    debug->addWidget(inspect);
    addToolBarBreak();
    addToolBar(debug);

    auto* dock = new QDockWidget(tr("Delta"), this);
    dock->setObjectName(QStringLiteral("delta"));
    dock->setWidget(new DeltaInspector(m_editor, dock));
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    QObject::connect(inspect, &QCheckBox::toggled, dock, &QDockWidget::setVisible);

    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);

    m_session = new CollabSession(m_editor, name, color, this);
    QObject::connect(m_session, &CollabSession::statusChanged, this, &CollabWindow::updateStatus);
    QObject::connect(m_latency, &QSpinBox::valueChanged, this, &CollabWindow::setLatency);
    QObject::connect(m_offline, &QCheckBox::toggled, this, [this](bool offline) {
        if (offline)
            m_session->disconnect();
        else
            m_session->reconnect(openChannel());
    });
    // A dropped connection (server restart, network): retry unless offline on purpose.
    QObject::connect(m_session, &CollabSession::disconnected, this, [this] {
        if (!m_offline->isChecked())
            QTimer::singleShot(1000, this, [this] {
                if (!m_offline->isChecked() && !m_session->isConnected())
                    m_session->reconnect(openChannel());
            });
    });
    m_session->connectTo(openChannel());
    resize(640, 480);
}

Channel* CollabWindow::openChannel()
{
    Channel* c = m_connect();
    c->setLatency(m_latency->value());
    return c;
}

void CollabWindow::setLatency(int ms)
{
    if (m_latency->value() != ms)
        m_latency->setValue(ms);
    if (auto* c = m_session->findChild<Channel*>())
        c->setLatency(ms);
}

void CollabWindow::updateStatus()
{
    const quill::OtClient& c = m_session->client();
    QString pending;
    switch (c.state()) {
    case quill::OtClient::State::Synchronized:
        pending = tr("synchronized");
        break;
    case quill::OtClient::State::AwaitingConfirm:
        pending = tr("1 change in flight");
        break;
    case quill::OtClient::State::AwaitingWithBuffer:
        pending = tr("1 change in flight + buffered");
        break;
    }
    m_status->setText(
        tr("%1 | %2 | revision %3 | %4 other(s) | %5")
            .arg(m_session->isConnected() ? tr("connected as %1").arg(m_session->clientId()) : tr("offline"))
            .arg(m_session->name())
            .arg(c.revision())
            .arg(m_session->peerCount())
            .arg(pending));
}

} // namespace collab
