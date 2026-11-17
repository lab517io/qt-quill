#pragma once

#include <QMainWindow>

#include <functional>

class QCheckBox;
class QLabel;
class QSpinBox;

namespace quill {
class QuillEditor;
}

namespace collab {

class Channel;
class CollabSession;

/// A client window: toolbar, editor, status line, and a debug bar with
/// artificial latency and an offline switch.
class CollabWindow : public QMainWindow
{
    Q_OBJECT
public:
    /// `connect` opens a new channel to the server (for each (re)connect).
    CollabWindow(const QString& name, const QColor& color, std::function<Channel*()> connect,
                 QWidget* parent = nullptr);

    quill::QuillEditor* editor() const { return m_editor; }
    CollabSession* session() const { return m_session; }
    void setLatency(int ms);

private:
    Channel* openChannel();
    void updateStatus();

    quill::QuillEditor* m_editor;
    CollabSession* m_session;
    std::function<Channel*()> m_connect;
    QLabel* m_status;
    QSpinBox* m_latency;
    QCheckBox* m_offline;
};

} // namespace collab
