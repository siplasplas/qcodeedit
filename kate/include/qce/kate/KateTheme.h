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
    QHash<QString, StyleEntry> styles;            ///< key: "Normal", "Keyword", …

    bool isValid() const { return !name.isEmpty(); }

    /// Parse a *.theme JSON file. Returns an invalid (empty name) KateTheme on error.
    static KateTheme load(const QString& path);

    /// Scan `dir` for *.theme files and return (displayName, filePath) pairs
    /// sorted alphabetically by display name.
    static QList<QPair<QString, QString>> listThemes(const QString& dir);
};
