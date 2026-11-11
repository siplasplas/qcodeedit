#pragma once

#include <memory>

#include <qce/IMargin.h>
#include <QFont>

namespace qce {

class ITextDocument;

/// Draws right-aligned line numbers using the same font as the editor.
/// Width auto-sizes to fit the digit count of the document's line count.
/// In word-wrap mode continuation rows get a short wrap arrow instead of a
/// number. Drawn in the painter's pen colour (the rail's foreground).
class LineNumberGutter : public IMargin {
public:
    ~LineNumberGutter();
    LineNumberGutter(const LineNumberGutter& other);
    LineNumberGutter& operator=(const LineNumberGutter& other);
    LineNumberGutter(LineNumberGutter&& other) noexcept;
    LineNumberGutter& operator=(LineNumberGutter&& other) noexcept;
    explicit LineNumberGutter(const ITextDocument* doc = nullptr);

    /// The document is used only to query lineCount() for width calculation.
    /// The gutter does not take ownership.
    void setDocument(const ITextDocument* doc);
    const ITextDocument* document() const;

    /// Font must match the editor font so metrics agree.
    void setFont(const QFont& font);
    const QFont& font() const;

    // IMargin
    int  preferredWidth(const ViewportState& vp) const override;
    void paint(QPainter& painter,
               const ViewportState& vp,
               const QRect& marginRect) override;

private:
    /// Number of decimal digits needed to represent lineCount.
    static int digitCount(int lineCount);

    /// Small hooked arrow marking a continuation row of a wrapped line,
    /// right-aligned at `right` and vertically centred on `centerY`.
    static void paintWrapArrow(QPainter& painter, int right, int centerY, int charWidth);

    // The state, in LineNumberGutter.cpp: fields added there keep the
    // binary interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace qce
