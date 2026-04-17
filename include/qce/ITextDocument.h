#pragma once

#include "TextCursor.h"

#include <QObject>
#include <QString>

namespace qce {

/// Abstract text document interface.
///
/// Separates the text storage backend from the view. This allows swapping
/// the implementation (QStringList in v0.2, gap buffer / piece table later)
/// without touching the rendering code.
///
/// Line indices are 0-based. A document with zero lines is valid (empty
/// document); a document with one empty line is distinct from an empty one.
class ITextDocument : public QObject {
    Q_OBJECT
public:
    explicit ITextDocument(QObject* parent = nullptr) : QObject(parent) {}
    ~ITextDocument() override = default;

    // --- Read interface ---

    /// Number of lines in the document. Returns 0 for an empty document.
    virtual int lineCount() const = 0;

    /// Returns the line at the given 0-based index. Empty if out of range.
    virtual QString lineAt(int index) const = 0;

    /// Maximum line length in QChar units. Default: linear scan.
    virtual int maxLineLength() const;

    // --- Mutating interface ---

    /// Inserts `text` at `pos`. Text may contain '\n' (splits lines).
    /// Emits linesChanged / linesInserted as appropriate.
    /// Returns the cursor position immediately after the inserted text.
    virtual TextCursor insertText(TextCursor pos, const QString& text) = 0;

    /// Removes text in the range [start, end). Returns the removed text
    /// (needed by undo commands to replay the removal).
    /// Emits linesChanged / linesRemoved as appropriate.
    virtual QString removeText(TextCursor start, TextCursor end) = 0;

    /// Removes trailing whitespace from every line. Not undoable — intended
    /// for "save with cleanup" workflows. Emits linesChanged per modified line.
    void stripTrailingWhitespace();

signals:
    void linesInserted(int startLine, int count);
    void linesRemoved(int startLine, int count);
    void linesChanged(int startLine, int count);
    void documentReset();
};

} // namespace qce
