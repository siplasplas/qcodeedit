#pragma once

#include "ITextDocument.h"

#include <QStringList>

namespace qce {

/// Simple ITextDocument implementation backed by QStringList.
///
/// All operations are O(n) in document size — fine for light editing but
/// unsuitable for very large files. Replace with gap buffer / piece table
/// for a production backend.
class SimpleTextDocument : public ITextDocument {
    Q_OBJECT
public:
    explicit SimpleTextDocument(QObject* parent = nullptr);
    ~SimpleTextDocument() override = default;

    // --- ITextDocument read interface ---
    int     lineCount()    const override;
    QString lineAt(int index) const override;
    int     maxLineLength() const override;

    // --- ITextDocument mutating interface ---
    TextCursor insertText(TextCursor pos, const QString& text) override;
    QString    removeText(TextCursor start, TextCursor end)    override;

    // --- Bulk operations ---

    /// Replaces the entire document. Emits documentReset.
    void setLines(QStringList lines);

    /// Replaces the entire document, splitting on '\n'. Trailing '\n' does
    /// not produce an extra empty line. '\r\n' is normalised to '\n'.
    /// Emits documentReset.
    void setText(const QString& text);

    /// Returns the document contents joined by '\n'. No trailing newline.
    QString toPlainText() const;

private:
    QStringList m_lines;
    mutable int m_maxLineLength = -1;

    void invalidateCache();

    // Clamp helpers (no signal side-effects).
    TextCursor clampPos(TextCursor pos) const;
};

} // namespace qce
