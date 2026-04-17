#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace qce {

/// Snapshot of the editor viewport at a given moment.
///
/// Published by CodeEditArea via viewportChanged() signal whenever the view
/// changes (scroll, resize, document modification). Margins (gutters, side
/// bars) consume this struct and use it to render themselves without needing
/// direct access to CodeEditArea internals.
///
/// All coordinates are in pixels unless noted. Line indices are 0-based.
struct ViewportState {
    /// Index of the first line at least partially visible in the viewport.
    int firstVisibleLine = 0;

    /// Index of the last line at least partially visible in the viewport.
    /// Equal to firstVisibleLine - 1 if the document is empty.
    int lastVisibleLine = -1;

    /// Vertical pixel offset of firstVisibleLine's top edge relative to the
    /// viewport's top. Typically <= 0 when the line is partially scrolled off.
    int contentOffsetY = 0;

    /// Horizontal pixel offset: the viewport starts drawing at this pixel
    /// position within each line. 0 means no horizontal scroll. Always >= 0.
    int contentOffsetX = 0;

    /// Width of a single character in pixels (mono-font assumption).
    int charWidth = 0;

    /// Height of a single line in pixels (mono-font, fixed line-height in v1).
    int lineHeight = 0;

    /// Total width of the viewport in pixels. Margins may use this for
    /// right-aligned drawing.
    int viewportWidth = 0;

    /// Total height of the viewport in pixels.
    int viewportHeight = 0;

    // -----------------------------------------------------------------------
    // Word-wrap additions. Ignored by margins that do not support wrap yet.
    // -----------------------------------------------------------------------

    /// True when the editor is in word-wrap mode.
    bool wordWrap = false;

    /// First / last visible visual row index (= firstVisibleLine/lastVisibleLine
    /// when wordWrap is false; may differ when wrap is active).
    int firstVisibleRow = 0;
    int lastVisibleRow  = -1;

    /// Per-visual-row descriptor. Populated only when wordWrap=true; empty
    /// otherwise. Element 0 corresponds to firstVisibleRow.
    struct RowInfo {
        int  logicalLine = -1; ///< document line; -1 when isFiller
        int  startCol    = 0;  ///< first logical column of this visual row
        int  endCol      = 0;  ///< one past last logical column
        bool isFirstRow  = false; ///< first visual row of logicalLine?
        /// When non-empty, this row is the header of a collapsed fold region
        /// and the renderer should draw the placeholder text starting at
        /// column `foldStartColumn`, replacing everything past it.
        QString foldPlaceholder;
        int     foldStartColumn = -1;

        /// Filler row: virtual line inserted between document lines, not
        /// numbered and never holding the caret. Renderer fills with
        /// fillerColor; margins skip it.
        bool    isFiller    = false;
        QColor  fillerColor;
        QString fillerLabel;   ///< non-empty only on the first row of a block
    };
    QVector<RowInfo> rows;

    /// Returns true if the viewport has a valid, renderable state.
    bool isValid() const noexcept {
        return lineHeight > 0 && viewportWidth > 0 && viewportHeight > 0;
    }

    /// Number of lines currently visible (fully or partially).
    int visibleLineCount() const noexcept {
        if (lastVisibleLine < firstVisibleLine) return 0;
        return lastVisibleLine - firstVisibleLine + 1;
    }
};

} // namespace qce
