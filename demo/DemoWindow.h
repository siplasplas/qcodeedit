#pragma once

#include <qce/kate/KateTheme.h>

#include <QMainWindow>

#include <memory>

namespace qce {
class CodeEdit;
class SimpleTextDocument;
class LineNumberGutter;
class FoldingGutter;
class RulesHighlighter;
class RuleBasedFoldingProvider;
}

class QAction;

class DemoWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DemoWindow(QWidget* parent = nullptr);
    ~DemoWindow() override;

    void loadFile(const QString& path);

private slots:
    void onFileOpen();
    void onFileClose();
    void onLoadSyntax();
    void onSelectTheme(const QString& themePath);
    void onScrollBarSideToggled(bool left);
    void onLineNumberSideToggled(bool left);

private:
    qce::CodeEdit*           m_editor      = nullptr;
    qce::SimpleTextDocument* m_doc         = nullptr;
    std::unique_ptr<qce::LineNumberGutter> m_lineNumbers;
    std::unique_ptr<qce::FoldingGutter>    m_foldGutter;
    std::unique_ptr<qce::RulesHighlighter> m_highlighter;
    std::unique_ptr<qce::RuleBasedFoldingProvider> m_foldProvider;
    QString    m_currentPath;
    QString    m_currentSyntaxPath;
    KateTheme  m_currentTheme;         ///< invalid = use built-in theme

    // Settings state
    bool m_lineNumbersOnLeft = true;

    // Settings actions (kept for initial check-state sync)
    QAction* m_actScrollLeft    = nullptr;
    QAction* m_actLineNumLeft   = nullptr;

    void buildMenus();
    void buildDemoHighlighter();
    void loadDemoText();
    void updateTitle();
    void applyThemeToEditor();
    void reloadSyntaxWithTheme();
};
