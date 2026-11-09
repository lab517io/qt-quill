#include "MainWindow.h"

#include "DeltaInspector.h"
#include "NotesEmbed.h"

#include <quill/DeltaDocument.h>
#include <quill/EmbedRegistry.h>
#include <quill/QuillEditor.h>
#include <quill/QuillFindBar.h>
#include <quill/QuillToolbar.h>

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTextBlock>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), m_editor(new quill::QuillEditor), m_findBar(new quill::QuillFindBar),
      m_status(new QLabel)
{
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_findBar);
    m_findBar->hide();
    setCentralWidget(central);

    m_toolbar = new quill::QuillToolbar(m_editor, this);
    m_toolbar->setFindBar(m_findBar);
    addToolBar(m_toolbar);

    m_inspector = new DeltaInspector(m_editor, this);
    m_dock = new QDockWidget(tr("Delta"), this);
    m_dock->setObjectName(QStringLiteral("inspector"));
    m_dock->setWidget(m_inspector);
    addDockWidget(Qt::RightDockWidgetArea, m_dock);
    resizeDocks({m_dock}, {380}, Qt::Horizontal);

    statusBar()->addWidget(m_status, 1);
    connect(m_editor, &quill::QuillEditor::editorChange, this, &MainWindow::updateStatus);
    connect(m_editor, &QTextEdit::cursorPositionChanged, this, &MainWindow::updateStatus);
    connect(m_editor->document(), &QTextDocument::modificationChanged, this, &MainWindow::updateTitle);
    connect(m_editor, &quill::QuillEditor::linkActivated, this,
            [this](const QUrl& url) { statusBar()->showMessage(tr("Link: %1").arg(url.toString()), 5000); });
    m_editor->embeds()->emplace<NotesEmbed>();
    connect(m_editor, &quill::QuillEditor::embedActivated, this,
            [this](const QString& type, const QJsonValue& data, int index) {
                const QString value = data.isString()
                                          ? data.toString()
                                          : QString::fromUtf8(QJsonDocument(QJsonObject{{type, data}}).toJson(QJsonDocument::Compact));
                statusBar()->showMessage(tr("Embed \"%1\" at %2: %3").arg(type).arg(index).arg(value.left(200)), 8000);
            });

    buildMenus();
    m_name = tr("Untitled");
    updateTitle();
    updateStatus();
    resize(1280, 800);
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&New"), QKeySequence::New, this, [this] {
        if (!maybeSave())
            return;
        m_editor->setContents(quill::Delta().insert(QStringLiteral("\n")));
        m_path.clear();
        m_name = tr("Untitled");
        m_inspector->clearChanges();
        updateTitle();
    });
    file->addAction(tr("&Open..."), QKeySequence::Open, this, [this] {
        if (!maybeSave())
            return;
        const QString path = QFileDialog::getOpenFileName(this, tr("Open Delta"), QString(), tr("Delta JSON (*.json);;All files (*)"));
        QString error;
        if (!path.isEmpty() && !openFile(path, &error))
            QMessageBox::warning(this, tr("Open"), error);
    });
    QMenu* samples = file->addMenu(tr("Load &sample"));
    const QDir dir(QStringLiteral(":/samples"));
    for (const QString& name : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        samples->addAction(name, this, [this, name] {
            if (maybeSave())
                loadSample(QStringLiteral(":/samples/") + name);
        });
    }
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, [this] { save(); });
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs, this, [this] { saveAs(); });
    m_opsShape = file->addAction(tr("Save as Quill.js {\"ops\": [...]}"));
    m_opsShape->setCheckable(true);
    m_opsShape->setToolTip(tr("Off: flutter_quill's bare array"));
    file->addSeparator();
    file->addAction(tr("Export &HTML..."), this, [this] { exportText(true); });
    file->addAction(tr("Export &plain text..."), this, [this] { exportText(false); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    edit->addAction(m_toolbar->action(QStringLiteral("undo")));
    edit->addAction(m_toolbar->action(QStringLiteral("redo")));
    edit->addSeparator();
    edit->addAction(tr("&Find"), QKeySequence::Find, m_findBar, &quill::QuillFindBar::activate);
    edit->addSeparator();
    edit->addAction(tr("Insert &custom embed..."), this, &MainWindow::insertCustomEmbed);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(m_dock->toggleViewAction());
    QAction* readOnly = view->addAction(tr("&Read-only"));
    readOnly->setCheckable(true);
    connect(readOnly, &QAction::toggled, this, [this](bool on) {
        m_editor->setReadOnly(on);
        m_toolbar->updateState();
    });
    m_dark = view->addAction(tr("&Dark palette"));
    m_dark->setCheckable(true);
    connect(m_dark, &QAction::toggled, this, &MainWindow::setDarkPalette);
}

void MainWindow::setDarkPalette(bool dark)
{
    static const QPalette light = QApplication::palette();
    if (m_dark->isChecked() != dark)
        m_dark->setChecked(dark); // re-enters through toggled()
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    if (!dark) {
        QApplication::setPalette(light);
    } else {
        QPalette p;
        const QColor window(0x2B, 0x2B, 0x2B), base(0x1E, 0x1E, 0x1E), text(0xE0, 0xE0, 0xE0);
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, window);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, window);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::ToolTipBase, base);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Highlight, QColor(0x2F, 0x65, 0xCA));
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link, QColor(0x64, 0xB5, 0xF6));
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
        p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
        QApplication::setPalette(p);
    }
    quill::StyleOptions style = dark ? quill::StyleOptions::dark() : quill::StyleOptions();
    m_editor->setStyleOptions(style);
}

bool MainWindow::loadJson(const QByteArray& json, const QString& name, QString* error)
{
    QString parseError;
    const auto delta = quill::Delta::fromJson(json, &parseError);
    if (!delta || !delta->isDocument()) {
        if (error)
            *error = tr("%1 is not a Delta document: %2").arg(name, parseError.isEmpty() ? tr("contains changes") : parseError);
        return false;
    }
    m_editor->setContents(*delta);
    m_editor->document()->setModified(false);
    m_editor->setSelection(0, 0, quill::Source::Silent);
    m_inspector->clearChanges();
    m_name = name;
    updateTitle();
    return true;
}

bool MainWindow::openFile(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    if (!loadJson(f.readAll(), QFileInfo(path).fileName(), error))
        return false;
    m_path = path;
    return true;
}

bool MainWindow::loadSample(const QString& resource)
{
    QFile f(resource);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    m_path.clear();
    return loadJson(f.readAll(), QFileInfo(resource).fileName(), nullptr);
}

bool MainWindow::maybeSave()
{
    if (!m_editor->document()->isModified())
        return true;
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to %1?").arg(m_name),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return save();
    return answer == QMessageBox::Discard;
}

bool MainWindow::save() { return m_path.isEmpty() ? saveAs() : writeFile(m_path); }

bool MainWindow::saveAs()
{
    const QString path = QFileDialog::getSaveFileName(this, tr("Save Delta"), m_path.isEmpty() ? m_name : m_path,
                                                      tr("Delta JSON (*.json)"));
    if (path.isEmpty() || !writeFile(path))
        return false;
    m_path = path;
    m_name = QFileInfo(path).fileName();
    updateTitle();
    return true;
}

bool MainWindow::writeFile(const QString& path)
{
    QSaveFile f(path);
    const auto shape = m_opsShape->isChecked() ? quill::Delta::JsonShape::OpsObject : quill::Delta::JsonShape::Array;
    if (!f.open(QIODevice::WriteOnly) || f.write(m_editor->getContents().toJson(shape)) < 0 || !f.commit()) {
        QMessageBox::warning(this, tr("Save"), tr("Cannot write %1: %2").arg(path, f.errorString()));
        return false;
    }
    m_editor->document()->setModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(path), 3000);
    return true;
}

void MainWindow::exportText(bool html)
{
    const QString path = QFileDialog::getSaveFileName(this, html ? tr("Export HTML") : tr("Export text"), QString(),
                                                      html ? tr("HTML (*.html)") : tr("Text (*.txt)"));
    if (path.isEmpty())
        return;
    QSaveFile f(path);
    const QString text = html ? m_editor->toHtml() : m_editor->getText().remove(QChar::ObjectReplacementCharacter);
    if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit())
        QMessageBox::warning(this, tr("Export"), f.errorString());
}

void MainWindow::insertCustomEmbed()
{
    if (m_editor->isReadOnly())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Insert custom embed"));
    auto* form = new QFormLayout(&dialog);
    auto* type = new QLineEdit(QStringLiteral("notes"), &dialog);
    auto* data = new QPlainTextEdit(&dialog);
    data->setPlainText(QStringLiteral("\"[{\\\"insert\\\":\\\"A note\\\\n\\\"}]\""));
    data->setToolTip(tr("JSON value; text that is not valid JSON is used as a string"));
    form->addRow(tr("Type:"), type);
    form->addRow(tr("Data (JSON):"), data);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || type->text().trimmed().isEmpty())
        return;
    // Parse as a JSON value (wrapped, so scalars parse too).
    const QJsonDocument parsed = QJsonDocument::fromJson("[" + data->toPlainText().toUtf8() + "]");
    const QJsonValue value = parsed.isArray() && parsed.array().size() == 1 ? parsed.array().first()
                                                                            : QJsonValue(data->toPlainText());
    const int index = m_editor->getSelection().index;
    m_editor->insertBlockEmbed(index, QStringLiteral("custom"),
                               quill::CustomEmbedRenderer::encode(type->text().trimmed(), value), quill::Source::User);
}

void MainWindow::updateTitle()
{
    setWindowTitle(tr("%1%2 - quill-demo").arg(m_name, m_editor->document()->isModified() ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateStatus()
{
    const quill::Range r = m_editor->getSelection();
    const QTextBlock block = m_editor->document()->findBlock(r.index);
    const int nl = block.position() + block.length() - 1;
    const quill::Delta line = m_editor->getContents(nl, 1);
    const QByteArray attrs = line.ops().isEmpty() || !line.ops().first().hasAttributes()
                                 ? QByteArray("{}")
                                 : QJsonDocument(line.ops().first().attributes()).toJson(QJsonDocument::Compact);
    m_status->setText(tr("Index %1   Selection %2   Length %3   Line %4")
                          .arg(r.index)
                          .arg(r.length)
                          .arg(m_editor->getLength())
                          .arg(QString::fromUtf8(attrs)));
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    if (maybeSave())
        e->accept();
    else
        e->ignore();
}
