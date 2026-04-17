#include "DemoWindow.h"

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>

#include <QAction>
#include <QActionGroup>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QTextStream>

DemoWindow::DemoWindow(QWidget* parent)
    : QMainWindow(parent) {
    m_doc = new qce::SimpleTextDocument(this);

    m_editor = new qce::CodeEdit(this);
    m_editor->setDocument(m_doc);

    m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
    m_lineNumbers->setFont(m_editor->area()->font());
    m_editor->addLeftMargin(m_lineNumbers.get());

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

// --- Slots ------------------------------------------------------------------

void DemoWindow::onFileOpen() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open file"), QString(),
        tr("Text files (*.txt *.md *.cpp *.h *.cmake);;All files (*)"));
    if (!path.isEmpty()) {
        loadFile(path);
    }
}

void DemoWindow::onFileClose() {
    m_doc->setLines({});
    m_currentPath.clear();
    updateTitle();
}

void DemoWindow::onScrollBarSideToggled(bool left) {
    using Side = qce::CodeEdit::ScrollBarSide;
    m_editor->setScrollBarSide(left ? Side::Left : Side::Right);
}

void DemoWindow::onLineNumberSideToggled(bool left) {
    if (left == m_lineNumbersOnLeft) {
        return;
    }
    m_lineNumbersOnLeft = left;
    if (left) {
        m_editor->removeRightMargin(m_lineNumbers.get());
        m_editor->addLeftMargin(m_lineNumbers.get());
    } else {
        m_editor->removeLeftMargin(m_lineNumbers.get());
        m_editor->addRightMargin(m_lineNumbers.get());
    }
}

void DemoWindow::onInvertSelectionToggled(bool invert) {
    m_editor->area()->setInvertSelection(invert);
}

// --- Private helpers --------------------------------------------------------

void DemoWindow::buildMenus() {
    // File menu
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

    // Settings menu
    auto* settingsMenu = menuBar()->addMenu(tr("&Settings"));

    // --- Scrollbar side ---
    auto* scrollMenu = settingsMenu->addMenu(tr("Scrollbar side"));
    auto* scrollGroup = new QActionGroup(this);
    scrollGroup->setExclusive(true);

    auto* scrollRight = scrollMenu->addAction(tr("Right (default)"));
    scrollRight->setCheckable(true);
    scrollRight->setChecked(true);
    scrollGroup->addAction(scrollRight);

    auto* scrollLeft = scrollMenu->addAction(tr("Left"));
    scrollLeft->setCheckable(true);
    scrollGroup->addAction(scrollLeft);

    connect(scrollGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        onScrollBarSideToggled(a->text() == tr("Left"));
    });

    // --- Line numbers side ---
    auto* lineNumMenu = settingsMenu->addMenu(tr("Line numbers side"));
    auto* lineNumGroup = new QActionGroup(this);
    lineNumGroup->setExclusive(true);

    auto* lineNumLeft = lineNumMenu->addAction(tr("Left (default)"));
    lineNumLeft->setCheckable(true);
    lineNumLeft->setChecked(true);
    lineNumGroup->addAction(lineNumLeft);

    auto* lineNumRight = lineNumMenu->addAction(tr("Right"));
    lineNumRight->setCheckable(true);
    lineNumGroup->addAction(lineNumRight);

    connect(lineNumGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        onLineNumberSideToggled(a->text().startsWith(tr("Left")));
    });

    // --- Invert selection ---
    settingsMenu->addSeparator();

    auto* invertAct = settingsMenu->addAction(tr("Invert selection colors"));
    invertAct->setCheckable(true);
    invertAct->setChecked(false);
    connect(invertAct, &QAction::toggled, this, &DemoWindow::onInvertSelectionToggled);
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
