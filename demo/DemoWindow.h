#pragma once

#include <QMainWindow>

#include <memory>

namespace qce {
class CodeEdit;
class SimpleTextDocument;
class LineNumberGutter;
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
    void onScrollBarSideToggled(bool left);
    void onLineNumberSideToggled(bool left);
    void onInvertSelectionToggled(bool invert);

private:
    qce::CodeEdit*           m_editor      = nullptr;
    qce::SimpleTextDocument* m_doc         = nullptr;
    std::unique_ptr<qce::LineNumberGutter> m_lineNumbers;
    QString m_currentPath;

    // Settings state
    bool m_lineNumbersOnLeft = true;

    // Settings actions (kept for initial check-state sync)
    QAction* m_actScrollLeft    = nullptr;
    QAction* m_actLineNumLeft   = nullptr;
    QAction* m_actInvertSel     = nullptr;

    void buildMenus();
    void updateTitle();
};
