#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

/// Represents a Kate syntax-highlighting theme loaded from a *.theme JSON file.
///
/// The theme maps defStyleNum names (without the leading "ds", e.g. "Normal",
/// "Keyword", "String") to visual properties used by KateXmlReader when
/// building a RulesHighlighter's attribute palette.
struct KateTheme {
    struct StyleEntry {
        QColor fg;
        QColor bg;            ///< invalid = transparent
        bool   bold      = false;
        bool   italic    = false;
        bool   underline = false;
    };

    QString                    name;              ///< display name from metadata
    QColor                     editorBackground;  ///< editor-colors.BackgroundColor
    QColor                     iconBorder;        ///< editor-colors.IconBorder (gutter)
    QColor                     lineNumbers;       ///< editor-colors.LineNumbers
    QColor                     separator;         ///< editor-colors.Separator
    QHash<QString, StyleEntry> styles;            ///< key: "Normal", "Keyword", …

    bool isValid() const { return !name.isEmpty(); }

    /// Parse a *.theme JSON file. Returns an invalid (empty name) KateTheme on error.
    static KateTheme load(const QString& path);

    /// Path of the default theme for the desktop colour scheme, in
    /// qce::kate::themesDir(): breeze-dark.theme for a dark desktop,
    /// breeze-light.theme otherwise. The file may not exist.
    static QString defaultThemePath();

    /// Loads defaultThemePath(), falling back to breeze-light.theme when the
    /// dark one is missing. Returns an invalid KateTheme when neither exists;
    /// the editor then keeps its built-in black on white colours.
    static KateTheme loadDefault();

    /// Scan `dir` for *.theme files and return (displayName, filePath) pairs
    /// sorted alphabetically by display name.
    static QList<QPair<QString, QString>> listThemes(const QString& dir);
};
