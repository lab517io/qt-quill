#pragma once

#include <QMainWindow>

class QAction;
class QDockWidget;
class QLabel;

namespace quill {
class QuillEditor;
class QuillFindBar;
class QuillToolbar;
} // namespace quill

class DeltaInspector;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    quill::QuillEditor* editor() const { return m_editor; }
    /// Loads a Delta JSON file (bare array or {"ops": [...]}).
    bool openFile(const QString& path, QString* error = nullptr);
    bool loadSample(const QString& resource);
    void setDarkPalette(bool dark);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    bool loadJson(const QByteArray& json, const QString& name, QString* error);
    bool maybeSave();
    bool save();
    bool saveAs();
    bool writeFile(const QString& path);
    void exportText(bool html);
    void insertCustomEmbed();
    void updateTitle();
    void updateStatus();

    quill::QuillEditor* m_editor;
    quill::QuillToolbar* m_toolbar;
    quill::QuillFindBar* m_findBar;
    DeltaInspector* m_inspector;
    QDockWidget* m_dock;
    QLabel* m_status;
    QAction* m_opsShape;
    QAction* m_dark;
    QString m_path;
    QString m_name;
};
