#include "DemoWindow.h"

#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/FoldState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/SimpleTextDocument.h>
#include <qce/margins/FoldingGutter.h>
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
#include <QPalette>
#include <QTextStream>

DemoWindow::DemoWindow(QWidget* parent)
    : QMainWindow(parent) {
    m_doc = new qce::SimpleTextDocument(this);

    m_editor = new qce::CodeEdit(this);
    m_editor->setDocument(m_doc);

    m_lineNumbers = std::make_unique<qce::LineNumberGutter>(m_doc);
    m_lineNumbers->setFont(m_editor->area()->font());
    m_editor->addLeftMargin(m_lineNumbers.get());

    m_foldGutter = std::make_unique<qce::FoldingGutter>(
        &m_editor->area()->foldState(),
        [this](int line) { m_editor->area()->toggleFoldAt(line); });
    m_editor->addLeftMargin(m_foldGutter.get());

    buildDemoHighlighter();
    m_editor->area()->setHighlighter(m_highlighter.get());
    m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
    m_foldProvider->setPlaceholderFor(QStringLiteral("curly"), QStringLiteral("{…}"));
    m_foldProvider->setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
    m_editor->area()->setFoldingProvider(m_foldProvider.get());
    m_editor->area()->setWordWrap(true);  // MVP: fold needs wrap mode for rendering

    // Demo line-background decorations: simulate a breakpoint, a "removed"
    // line and a "changed" line so the per-line background API is visible.
    m_editor->area()->setLineBackgroundProvider([](int line) -> QColor {
        switch (line) {
        case 9:  return QColor("#FFE0E0"); // breakpoint (soft red)
        case 10: return QColor("#D4F4DD"); // diff: added   (soft green)
        case 13: return QColor("#C2D8F2"); // diff: changed (soft blue)
        default: return {};
        }
    });


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
    m_currentSyntaxPath = path;
    reloadSyntaxWithTheme();
}

void DemoWindow::onSelectTheme(const QString& themePath) {
    if (themePath.isEmpty()) {
        m_currentTheme = KateTheme{};
    } else {
        KateTheme t = KateTheme::load(themePath);
        if (!t.isValid()) return;
        m_currentTheme = std::move(t);
    }
    applyThemeToEditor();
    if (!m_currentSyntaxPath.isEmpty()) {
        reloadSyntaxWithTheme();
    } else {
        // No Kate XML loaded — rebuild the built-in demo highlighter with
        // the new theme's colors and keep it active.
        buildDemoHighlighter();
        m_editor->area()->setHighlighter(m_highlighter.get());
        m_foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(m_highlighter.get());
        m_foldProvider->setPlaceholderFor(QStringLiteral("curly"),   QStringLiteral("{…}"));
        m_foldProvider->setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
        m_editor->area()->setFoldingProvider(m_foldProvider.get());
    }
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

    // Edit menu (folding)
    auto* editMenu = menuBar()->addMenu(tr("&Edit"));

    auto* toggleFoldAct = editMenu->addAction(tr("&Toggle fold at cursor"));
    toggleFoldAct->setShortcut(QKeySequence(tr("F9")));
    connect(toggleFoldAct, &QAction::triggered, this, [this]{
        m_editor->area()->toggleFoldAt(m_editor->area()->cursorPosition().line);
    });

    auto* foldAllAct = editMenu->addAction(tr("&Fold all"));
    foldAllAct->setShortcut(QKeySequence(tr("Ctrl+Shift+-")));
    connect(foldAllAct, &QAction::triggered, this, [this]{
        m_editor->area()->foldAll();
    });

    auto* unfoldAllAct = editMenu->addAction(tr("&Unfold all"));
    unfoldAllAct->setShortcut(QKeySequence(tr("Ctrl+Shift+=")));
    connect(unfoldAllAct, &QAction::triggered, this, [this]{
        m_editor->area()->unfoldAll();
    });

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

    settingsMenu->addSeparator();

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

    auto* roAct = settingsMenu->addAction(tr("Read only"));
    roAct->setCheckable(true);
    roAct->setChecked(false);
    connect(roAct, &QAction::toggled, m_editor->area(),
            &qce::CodeEditArea::setReadOnly);

    // Theme menu
    const QString themesDir =
        QDir::homePath() +
        QStringLiteral("/.local/share/org.kde.syntax-highlighting/themes");
    const auto themes = KateTheme::listThemes(themesDir);
    if (!themes.isEmpty()) {
        auto* themeMenu = menuBar()->addMenu(tr("&Theme"));
        auto* themeGroup = new QActionGroup(this);
        themeGroup->setExclusive(true);

        auto* builtinAct = themeMenu->addAction(tr("(built-in)"));
        builtinAct->setCheckable(true);
        builtinAct->setChecked(true);
        themeGroup->addAction(builtinAct);
        connect(builtinAct, &QAction::triggered, this,
                [this] { onSelectTheme(QString{}); });

        themeMenu->addSeparator();

        for (const auto& [name, path] : themes) {
            auto* act = themeMenu->addAction(name);
            act->setCheckable(true);
            themeGroup->addAction(act);
            const QString p = path;
            connect(act, &QAction::triggered, this,
                    [this, p] { onSelectTheme(p); });
        }
    }
}

static qce::TextAttribute demoAttr(const KateTheme& theme,
                                    const QString& styleKey,
                                    QColor fallbackFg,
                                    bool fallbackBold = false,
                                    bool fallbackItalic = false) {
    if (theme.isValid()) {
        const auto it = theme.styles.constFind(styleKey);
        if (it != theme.styles.constEnd()) {
            qce::TextAttribute a;
            a.foreground = it->fg.isValid() ? it->fg : fallbackFg;
            a.bold       = it->bold;
            a.italic     = it->italic;
            return a;
        }
    }
    return {fallbackFg, {}, fallbackBold, fallbackItalic};
}

void DemoWindow::buildDemoHighlighter() {
    using namespace qce;
    m_highlighter = std::make_unique<RulesHighlighter>();

    // --- Palette: use theme colors when a theme is active ---
    const int attrKw      = m_highlighter->addAttribute(
        demoAttr(m_currentTheme, QStringLiteral("Keyword"),
                 QColor(0x00, 0x00, 0xAA), true));
    const int attrType    = m_highlighter->addAttribute(
        demoAttr(m_currentTheme, QStringLiteral("DataType"),
                 QColor(0x00, 0x80, 0x80), true));
    const int attrString  = m_highlighter->addAttribute(
        demoAttr(m_currentTheme, QStringLiteral("String"),
                 QColor(0xC0, 0x10, 0x10)));
    const int attrComment = m_highlighter->addAttribute(
        demoAttr(m_currentTheme, QStringLiteral("Comment"),
                 QColor(0x80, 0x80, 0x80), false, true));
    const int attrNumber  = m_highlighter->addAttribute(
        demoAttr(m_currentTheme, QStringLiteral("DecVal"),
                 QColor(0x80, 0x40, 0x00)));

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

void DemoWindow::applyThemeToEditor() {
    QPalette pal = m_editor->palette();

    const QColor bg = m_currentTheme.isValid() && m_currentTheme.editorBackground.isValid()
                      ? m_currentTheme.editorBackground
                      : QColor(Qt::white);
    pal.setColor(QPalette::Base,   bg);
    pal.setColor(QPalette::Window, bg);

    // Set default text color from theme's Normal style so that text rendered
    // without an explicit syntax span (attributeId == -1 or invalid fg)
    // uses the correct foreground — critical for dark themes.
    QColor fg(Qt::black);
    if (m_currentTheme.isValid()) {
        const auto it = m_currentTheme.styles.constFind(QStringLiteral("Normal"));
        if (it != m_currentTheme.styles.constEnd() && it->fg.isValid())
            fg = it->fg;
    }
    pal.setColor(QPalette::Text,       fg);
    pal.setColor(QPalette::WindowText, fg);

    m_editor->setPalette(pal);
    m_editor->area()->viewport()->setPalette(pal);
}

void DemoWindow::reloadSyntaxWithTheme() {
    if (m_currentSyntaxPath.isEmpty()) return;

    std::unique_ptr<qce::RulesHighlighter> hl;
    if (m_currentTheme.isValid())
        hl = KateXmlReader::load(m_currentSyntaxPath, m_currentTheme);
    else
        hl = KateXmlReader::load(m_currentSyntaxPath);

    if (!hl) {
        QMessageBox::warning(this, tr("Load failed"),
                             tr("Could not parse %1").arg(m_currentSyntaxPath));
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

void DemoWindow::updateTitle() {
    const QString appName = QStringLiteral("qcodeedit demo");
    if (m_currentPath.isEmpty()) {
        setWindowTitle(appName);
    } else {
        const QString name = QFileInfo(m_currentPath).fileName();
        setWindowTitle(QStringLiteral("%1 \u2014 %2").arg(name, appName));
    }
}
