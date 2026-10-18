#pragma once

#include <QColor>

namespace qce {

/// Visual style for a range of characters produced by a syntax highlighter.
/// Used as a palette entry: highlighters emit StyleSpans referring to a
/// TextAttribute by index into the palette.
struct TextAttribute {
    QColor foreground;            ///< main glyph color
    QColor background;            ///< invalid = no background (transparent)
    bool   bold      = false;
    bool   italic    = false;
    bool   underline = false;
    /// Text in this attribute may be spell checked (comments, strings and
    /// documentation in code; prose in markup). See CodeEditArea::spellCheckRanges().
    bool   spellCheck = true;
};

} // namespace qce
