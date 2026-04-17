#include "DemoWindow.h"

#include "KateXmlReader.h"

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/FoldState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/LineNumberGutter.h>

#include <QAction>
#include <QActionGroup>
#include <QDir>
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

    buildDemoHighlighter();
    m_editor->area()->setHighlighter(m_highlighter.get());
    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor(QStringLiteral("curly"), QStringLiteral("{…}"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
    m_editor->area()->setFoldingProvider(m_foldProvider.get());
    m_editor->area()->setWordWrap(true);  // MVP: fold needs wrap mode for rendering

    setCentralWidget(m_editor);
    buildMenus();
    loadDemoText();
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
        tr("Text files (*.txt *.md *.cpp *.h *.cmake);;All files (*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) {
        loadFile(path);
    }
}

void DemoWindow::onFileClose() {
    m_doc->setLines({});
    m_currentPath.clear();
    updateTitle();
}

void DemoWindow::onLoadSyntax() {
    const QString initial =
        QDir::homePath() + QStringLiteral("/.local/share/org.kde.syntax-highlighting/syntax");
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Kate syntax XML"), initial,
        tr("Kate syntax (*.xml);;All files (*)"),
        nullptr, QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;
    auto hl = KateXmlReader::load(path);
    if (!hl) {
        QMessageBox::warning(this, tr("Load failed"),
                             tr("Could not parse %1").arg(path));
        return;
    }
    m_highlighter = std::move(hl);
    m_editor->area()->setHighlighter(m_highlighter.get());
    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor(QStringLiteral("Brace1"),  QStringLiteral("{…}"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("curly"),   QStringLiteral("{…}"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("square"),  QStringLiteral("[…]"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("paren"),   QStringLiteral("(…)"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
    m_editor->area()->setFoldingProvider(m_foldProvider.get());
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

    auto* loadSyntaxAct = fileMenu->addAction(tr("&Load Kate syntax..."));
    connect(loadSyntaxAct, &QAction::triggered, this, &DemoWindow::onLoadSyntax);

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

    auto* wrapAct = settingsMenu->addAction(tr("Word wrap"));
    wrapAct->setCheckable(true);
    wrapAct->setChecked(false);
    connect(wrapAct, &QAction::toggled, m_editor->area(),
            &qce::CodeEditArea::setWordWrap);

    auto* wsAct = settingsMenu->addAction(tr("Show whitespace"));
    wsAct->setCheckable(true);
    wsAct->setChecked(false);
    connect(wsAct, &QAction::toggled, m_editor->area(),
            &qce::CodeEditArea::setShowWhitespace);
}

void DemoWindow::buildDemoHighlighter() {
    using namespace qce;
    m_highlighter = std::make_unique<RulesHighlighter>();

    // --- Palette (muted "classic" colors that show on both light/dark) ---
    const int attrKw      = m_highlighter->addAttribute(
        {QColor(0x00, 0x00, 0xAA), {}, /*bold*/true});
    const int attrType    = m_highlighter->addAttribute(
        {QColor(0x00, 0x80, 0x80), {}, /*bold*/true});
    const int attrString  = m_highlighter->addAttribute(
        {QColor(0xC0, 0x10, 0x10)});
    const int attrComment = m_highlighter->addAttribute(
        {QColor(0x80, 0x80, 0x80), {}, false, /*italic*/true});
    const int attrNumber  = m_highlighter->addAttribute(
        {QColor(0x80, 0x40, 0x00)});

    // --- Keyword lists ---
    const int klKeywords = m_highlighter->addKeywordList({"keywords", {
        QStringLiteral("if"),    QStringLiteral("else"), QStringLiteral("return"),
        QStringLiteral("for"),   QStringLiteral("while"), QStringLiteral("do"),
        QStringLiteral("break"), QStringLiteral("continue"), QStringLiteral("switch"),
        QStringLiteral("case"),  QStringLiteral("default"),
    }, true});
    const int klTypes = m_highlighter->addKeywordList({"types", {
        QStringLiteral("int"),  QStringLiteral("void"), QStringLiteral("char"),
        QStringLiteral("bool"), QStringLiteral("const"), QStringLiteral("float"),
        QStringLiteral("double"),
    }, true});

    // --- Contexts (created empty, rules appended after all ids are known) ---
    HighlightContext ctxN{"Normal",       -1,         -1, 0, false, -1, {}};
    HighlightContext ctxS{"String",       attrString, -1, 0, false, -1, {}};
    HighlightContext ctxB{"BlockComment", attrComment, -1, 0, false, -1, {}};
    HighlightContext ctxL{"LineComment",  attrComment,  0, 1, false, -1, {}};
    // LineComment: at end of line, #pop back to Normal.

    const int ctxNormal       = m_highlighter->addContext(ctxN);
    const int ctxString       = m_highlighter->addContext(ctxS);
    const int ctxBlockComment = m_highlighter->addContext(ctxB);
    const int ctxLineComment  = m_highlighter->addContext(ctxL);

    auto& normal = m_highlighter->contextRef(ctxNormal);
    // Line comment //...
    normal.rules.push_back(
        {HighlightRule::Detect2Chars, '/', '/', {}, true, {}, -1, -1,
         attrComment, ctxLineComment, 0, false, false});
    // Block comment /* ... */
    normal.rules.push_back(
        {HighlightRule::Detect2Chars, '/', '*', {}, true, {}, -1, -1,
         attrComment, ctxBlockComment, 0, false, false});
    // String "..."
    normal.rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrString, ctxString, 0, false, false});
    // Types (tried before keywords because both match identifiers)
    normal.rules.push_back(
        {HighlightRule::Keyword, {}, {}, {}, true, {}, klTypes, -1,
         attrType, -1, 0, false, false});
    // Keywords
    normal.rules.push_back(
        {HighlightRule::Keyword, {}, {}, {}, true, {}, klKeywords, -1,
         attrKw, -1, 0, false, false});
    // Numbers
    {
        HighlightRule r;
        r.kind = HighlightRule::Int;
        r.attributeId = attrNumber;
        normal.rules.push_back(r);
    }

    auto& strCtx = m_highlighter->contextRef(ctxString);
    // Escape sequences inside strings (\n, \t, \", \\, \xNN, ...)
    strCtx.rules.push_back(
        {HighlightRule::HlCStringChar, {}, {}, {}, true, {}, -1, -1,
         -1, -1, 0, false, false});
    // Closing quote
    strCtx.rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrString, -1, 1, false, false});

    auto& blockCtx = m_highlighter->contextRef(ctxBlockComment);
    // Closing */
    blockCtx.rules.push_back(
        {HighlightRule::Detect2Chars, '*', '/', {}, true, {}, -1, -1,
         attrComment, -1, 1, false, false});
    // LineComment has no rules beyond the default attribute and lineEnd #pop.

    // Folding: braces and block comment.
    const int rgCurly = m_highlighter->regionIdForName(QStringLiteral("curly"));
    const int rgComm  = m_highlighter->regionIdForName(QStringLiteral("Comment"));

    HighlightRule openBrace;
    openBrace.kind = HighlightRule::DetectChar;
    openBrace.ch = QLatin1Char('{');
    openBrace.beginRegionId = rgCurly;
    m_highlighter->contextRef(ctxNormal).rules.push_back(openBrace);

    HighlightRule closeBrace;
    closeBrace.kind = HighlightRule::DetectChar;
    closeBrace.ch = QLatin1Char('}');
    closeBrace.endRegionId = rgCurly;
    m_highlighter->contextRef(ctxNormal).rules.push_back(closeBrace);

    // Mark the existing /* and */ rules with region markers.
    // (The block-open rule is the second entry in the Normal context; the
    // block-close rule is the only entry in BlockComment.)
    m_highlighter->contextRef(ctxNormal).rules[1].beginRegionId = rgComm;
    m_highlighter->contextRef(ctxBlockComment).rules[0].endRegionId = rgComm;

    m_highlighter->setInitialContextId(ctxNormal);
}

void DemoWindow::loadDemoText() {
    m_doc->setText(QStringLiteral(
        "/* Multi-line block comment.\n"
        " * A \"fake string\" inside a comment is NOT highlighted.\n"
        " * Continues across several lines until the closing token.\n"
        " */\n"
        "\n"
        "// Single-line comment with a /* fake block start */ inside.\n"
        "\n"
        "int main(int argc, char** argv) {\n"
        "    const char* greeting = \"Hello, world!\\n\";\n"
        "    int count = 42;\n"
        "    if (argc > 1) {\n"
        "        return 0;\n"
        "    }\n"
        "    while (count > 0) {\n"
        "        count = count - 1;\n"
        "    }\n"
        "    return 1;\n"
        "}\n"));
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
