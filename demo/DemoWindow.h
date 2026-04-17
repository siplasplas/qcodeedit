#pragma once

#include <QMainWindow>

namespace qce {
class CodeEdit;
class SimpleTextDocument;
}

/// Main window for the qcodeedit demo application.
///
/// Provides a minimal File menu (Open, Close, Quit) around a qce::CodeEdit.
/// Extracted from main.cpp to keep main.cpp small and to demonstrate how
/// consumers typically wrap the editor widget.
class DemoWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit DemoWindow(QWidget* parent = nullptr);
    ~DemoWindow() override;

    /// Loads the given file into the editor. On failure, shows a message box
    /// and leaves the current document untouched.
    void loadFile(const QString& path);

private slots:
    void onFileOpen();
    void onFileClose();

private:
    qce::CodeEdit* m_editor = nullptr;
    qce::SimpleTextDocument* m_doc = nullptr;
    QString m_currentPath;

    /// Builds the menu bar. Called once from the constructor.
    void buildMenus();

    /// Updates the window title to reflect the currently loaded file.
    void updateTitle();
};
