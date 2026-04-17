#pragma once

#include "ITextDocument.h"

#include <QStringList>

namespace qce {

/// Simple ITextDocument implementation backed by QStringList.
///
/// Intended for v1 / MVP. All operations are O(n) in document size at worst,
/// which is fine for viewing and light editing but unsuitable for very large
/// files. Will be replaced by a gap-buffer or piece-table backend later.
///
/// The class is intentionally minimal; edit operations will be added in a
/// later milestone. v1 exposes only loading and read access.
class SimpleTextDocument : public ITextDocument {
    Q_OBJECT
public:
    explicit SimpleTextDocument(QObject* parent = nullptr);
    ~SimpleTextDocument() override = default;

    // --- ITextDocument interface ---
    int lineCount() const override;
    QString lineAt(int index) const override;
    int maxLineLength() const override;

    // --- Loading / bulk operations ---

    /// Replaces the entire document with the given lines. Emits documentReset.
    void setLines(QStringList lines);

    /// Replaces the entire document with the given text, splitting on '\n'.
    /// A trailing '\n' does not create an extra empty line. '\r\n' is
    /// normalized to '\n'. Emits documentReset.
    void setText(const QString& text);

    /// Returns the document contents joined by '\n'. No trailing newline.
    QString toPlainText() const;

private:
    QStringList m_lines;

    /// Cached maximum line length; -1 means "not computed / dirty".
    /// Mutable because maxLineLength() is logically const.
    mutable int m_maxLineLength = -1;

    /// Invalidates the maxLineLength cache. Called after any mutation.
    void invalidateCache();
};

} // namespace qce
