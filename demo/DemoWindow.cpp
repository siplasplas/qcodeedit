#include "DemoWindow.h"

#include <qce/CodeEdit.h>
#include <qce/SimpleTextDocument.h>

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextStream>

DemoWindow::DemoWindow(QWidget* parent)
    : QMainWindow(parent) {
    m_doc = new qce::SimpleTextDocument(this);

    m_editor = new qce::CodeEdit(this);
    m_editor->setDocument(m_doc);
    setCentralWidget(m_editor);

    buildMenus();
    updateTitle();
    resize(900, 600);
}

DemoWindow::~DemoWindow() = default;

void DemoWindow::loadFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Open failed"),
                             tr("Cannot open file: %1").arg(path));
        return;
    }
    QTextStream ts(&f);
    m_doc->setText(ts.readAll());
    m_currentPath = path;
    updateTitle();
}

// --- Slots --------------------------------------------------------------

void DemoWindow::onFileOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open file"), QString(),
        tr("Text files (*.txt *.md *.cpp *.h *.cmake);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    loadFile(path);
}

void DemoWindow::onFileClose() {
    m_doc->setLines({});
    m_currentPath.clear();
    updateTitle();
}

// --- Private helpers ----------------------------------------------------

void DemoWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* openAct = fileMenu->addAction(tr("&Open..."));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &DemoWindow::onFileOpen);

    auto* closeAct = fileMenu->addAction(tr("&Close"));
    closeAct->setShortcut(QKeySequence::Close);
    connect(closeAct, &QAction::triggered, this, &DemoWindow::onFileClose);

    fileMenu->addSeparator();

    auto* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);
}

void DemoWindow::updateTitle() {
    const QString appName = QStringLiteral("qcodeedit demo");
    if (m_currentPath.isEmpty()) {
        setWindowTitle(appName);
    } else {
        const QString name = QFileInfo(m_currentPath).fileName();
        setWindowTitle(QStringLiteral("%1 \u2014 %2").arg(name, appName));
    }
}
