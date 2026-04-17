#pragma once

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
