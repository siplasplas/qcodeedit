#pragma once

#include <qce/IMargin.h>
#include <QFont>

namespace qce {

class ITextDocument;

/// Draws right-aligned line numbers using the same font as the editor.
/// Width auto-sizes to fit the digit count of the document's line count.
class LineNumberGutter : public IMargin {
public:
    explicit LineNumberGutter(const ITextDocument* doc = nullptr);

    /// The document is used only to query lineCount() for width calculation.
    /// The gutter does not take ownership.
    void setDocument(const ITextDocument* doc);
    const ITextDocument* document() const { return m_doc; }

    /// Font must match the editor font so metrics agree.
    void setFont(const QFont& font);
    const QFont& font() const { return m_font; }

    // IMargin
    int  preferredWidth(const ViewportState& vp) const override;
    void paint(QPainter& painter,
               const ViewportState& vp,
               const QRect& marginRect) override;

private:
    /// Number of decimal digits needed to represent lineCount.
    static int digitCount(int lineCount);

    const ITextDocument* m_doc = nullptr;
    QFont m_font;
};

} // namespace qce
