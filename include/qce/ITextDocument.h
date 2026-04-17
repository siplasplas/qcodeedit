#pragma once

#include <QObject>
#include <QString>

namespace qce {

/// Abstract text document interface.
///
/// Separates the text storage backend from the view. This allows swapping the
/// implementation (QStringList in v1, gap buffer / piece table later) without
/// touching the rendering code.
///
/// The interface is line-oriented: the smallest addressable unit is a line.
/// This matches the typical code-editor usage pattern and simplifies the v1
/// implementation on top of QStringList.
///
/// Line indices are 0-based. A document with zero lines is valid (empty
/// document); a document with one empty line is distinct from an empty one.
class ITextDocument : public QObject {
    Q_OBJECT
public:
    explicit ITextDocument(QObject* parent = nullptr) : QObject(parent) {}
    ~ITextDocument() override = default;

    /// Number of lines in the document. Returns 0 for an empty document.
    virtual int lineCount() const = 0;

    /// Returns the line at the given 0-based index. Returns an empty QString
    /// if the index is out of range (callers should check lineCount() first).
    virtual QString lineAt(int index) const = 0;

    /// Convenience: returns the maximum line length (in QChar units) across
    /// all lines. Used by the view to compute horizontal scroll extent.
    /// Default implementation scans all lines; backends may override with an
    /// O(1) cached value.
    virtual int maxLineLength() const;

signals:
    /// Emitted when `count` lines were inserted starting at `startLine`.
    /// After emission, existing lines at startLine and after are shifted down.
    void linesInserted(int startLine, int count);

    /// Emitted when `count` lines were removed starting at `startLine`.
    /// After emission, lines at startLine+count and after are shifted up.
    void linesRemoved(int startLine, int count);

    /// Emitted when existing lines in the range [startLine, startLine+count)
    /// have their contents replaced. Line count is unchanged.
    void linesChanged(int startLine, int count);

    /// Emitted when the entire document has been replaced (e.g. on file load).
    /// Listeners should do a full re-render and discard any cached per-line
    /// state.
    void documentReset();
};

} // namespace qce
