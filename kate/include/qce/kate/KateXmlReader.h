#pragma once

#include <qce/RulesHighlighter.h>

#include <memory>

class QString;
struct KateTheme;
namespace qce::kate { class KateSyntaxIndex; }

class KateXmlReader {
public:
    /// Load using the built-in light theme.
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path);

    /// Load using an external KateTheme. Falls back to the built-in theme for
    /// any style not present in `theme`.
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path,
                                                        const KateTheme& theme);

    /// As above, but resolve cross-language ##Name includes through `index`
    /// instead of scanning the directory of `path`. `index` must outlive the
    /// call only (not the returned highlighter).
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path,
                                                        const KateTheme& theme,
                                                        const qce::kate::KateSyntaxIndex& index);
};
