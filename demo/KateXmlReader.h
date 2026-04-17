#pragma once

#include <qce/RulesHighlighter.h>

#include <memory>

class QString;

/// Parses a Kate Syntax XML file (e.g. ~/.local/share/org.kde.syntax-highlighting/
/// syntax/c.xml) and returns a ready-to-use RulesHighlighter.
///
/// Lives in the demo binary — core libqcodeedit is intentionally unaware of
/// the Kate XML format. Any application that wants to use Kate files links
/// this reader (or writes its own) and feeds the resulting highlighter to
/// CodeEditArea::setHighlighter().
///
/// Supported (v1):
/// - <itemDatas>/<itemData> with a small built-in theme (defStyleNum → QColor)
/// - <list>/<item> keyword lists
/// - <contexts>/<context> with rules: DetectChar, Detect2Chars, AnyChar,
///   StringDetect, WordDetect, RegExpr, keyword, DetectSpaces,
///   DetectIdentifier, Int, Float, HlCStringChar, LineContinue, RangeDetect,
///   IncludeRules (same-file only).
/// - context switches: #stay, #pop, #pop#pop, Name, #pop!Name
/// - lookAhead, firstNonSpace, insensitive rule attributes
/// - DTD entities (&int;, &symbols; …) pre-expanded in regex bodies
///
/// Ignored (silently):
/// - IncludeRules with "##OtherLanguage" (cross-file)
/// - beginRegion/endRegion (folding — future)
/// - dynamic, column, weakDeliminator, additionalDeliminator attributes
class KateXmlReader {
public:
    /// Returns nullptr on parse error (and prints a message to qWarning).
    static std::unique_ptr<qce::RulesHighlighter> load(const QString& path);
};
