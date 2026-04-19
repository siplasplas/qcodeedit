#pragma once

#include <qce/RulesHighlighter.h>

#include <memory>

class QString;
struct KateTheme;

class KateXmlReader {
public:
    /// Load using the built-in light theme.
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path);

    /// Load using an external KateTheme. Falls back to the built-in theme for
    /// any style not present in `theme`.
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path,
                                                        const KateTheme& theme);
};
