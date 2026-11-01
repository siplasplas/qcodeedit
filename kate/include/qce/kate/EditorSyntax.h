#pragma once

#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateTheme.h>
#include <qce/TextAttribute.h>

#include <QPalette>
#include <QString>

#include <memory>

namespace qce {
class CodeEdit;
class RulesHighlighter;
class RuleBasedFoldingProvider;
}

namespace qce::kate {

/// The index of the Kate syntax definitions in dataDir(), shared by the whole
/// program; loaded on first use.
const KateSyntaxIndex& syntaxIndex();
/// Reads the index and the syntax directory again (e.g. after a download).
void reloadSyntaxIndex();

/// Syntax colours, folding and theme of one CodeEdit from the Kate data:
/// the definition is chosen by the file name (or set by hand), the colours
/// come from a Kate theme (Breeze Light or Dark to match the desktop by
/// default), applied to the editor's palette and gutter as well.
///
/// Shared by editors built on qcodeedit (qceditor's tabs, the siplasplas
/// editor plugin), so that they colour files alike. Without Kate data, C and
/// C++ files get a small built-in highlighter, other files none.
class EditorSyntax {
public:
    enum class Mode {
        Automatic, ///< by the file name
        PlainText, ///< no colours
        Manual     ///< the definition set by setManual()
    };

    /// Attaches to `edit` (not owned; must outlive this object) and applies
    /// the default theme.
    explicit EditorSyntax(CodeEdit* edit);
    ~EditorSyntax();
    EditorSyntax(const EditorSyntax&) = delete;
    EditorSyntax& operator=(const EditorSyntax&) = delete;

    /// The file whose name chooses the definition in Automatic mode.
    void setFileName(const QString& path) { m_fileName = path; }
    QString fileName() const { return m_fileName; }

    Mode mode() const { return m_mode; }
    void setAutomatic();
    void setPlainText();
    /// `syntaxFile` is a definition's file name in syntaxIndex()
    /// (LanguageEntry::file), e.g. "cpp.xml".
    void setManual(const QString& syntaxFile);
    /// The definition set by setManual(); empty in the other modes.
    QString syntaxFile() const { return m_syntaxFile; }

    /// A *.theme file; empty = the default theme for the desktop.
    void setThemeFile(const QString& path) { m_themeFile = path; }
    QString themeFile() const { return m_themeFile; }
    const KateTheme& theme() const { return m_theme; }

    /// Loads the theme and the definition again and applies them, e.g. after
    /// the mode, the file name or the theme changed, or the Kate data was
    /// downloaded (reloadSyntaxIndex() first).
    void apply();

private:
    void applyThemePalette();
    TextAttribute themedAttribute(const QString& style, const TextAttribute& fallback) const;
    void applyForFile(const QString& path);
    void applyCpp();
    bool applyKate(const QString& xmlPath);
    void install(std::unique_ptr<RulesHighlighter> highlighter,
                 const QList<QPair<QString, QString>>& placeholders);
    void clear();

    CodeEdit* m_edit;
    QPalette m_defaultPalette;
    KateTheme m_theme;
    QString m_themeFile;
    QString m_fileName;
    QString m_syntaxFile;
    Mode m_mode = Mode::Automatic;
    std::unique_ptr<RulesHighlighter> m_highlighter;
    std::unique_ptr<RuleBasedFoldingProvider> m_foldProvider;
};

} // namespace qce::kate
