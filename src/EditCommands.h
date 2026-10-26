#pragma once

#include <qce/TextCursor.h>

#include <QUndoCommand>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace qce {

class ITextDocument;

/// Inserts text into the document. Consecutive single-line inserts at
/// adjacent positions are merged into one undo step.
class InsertCommand : public QUndoCommand {
public:
    InsertCommand(ITextDocument* doc,
                  TextCursor insertPos,
                  const QString& text,
                  TextCursor cursorBefore,
                  TextCursor* viewCursor,
                  TextCursor* viewAnchor);

    void redo() override;
    void undo() override;
    int  id()   const override { return 1; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    ITextDocument* m_doc;
    TextCursor m_insertPos;
    QString    m_text;
    TextCursor m_endPos;       // position after insertion, filled on first redo
    TextCursor m_cursorBefore; // cursor state to restore on undo
    TextCursor* m_viewCursor;
    TextCursor* m_viewAnchor;
};

/// Removes a range of text from the document. Stores the removed text so
/// undo can re-insert it exactly.
class RemoveCommand : public QUndoCommand {
public:
    RemoveCommand(ITextDocument* doc,
                  TextCursor start,
                  TextCursor end,
                  TextCursor cursorBefore,
                  TextCursor* viewCursor,
                  TextCursor* viewAnchor);

    void redo() override;
    void undo() override;

private:
    ITextDocument* m_doc;
    TextCursor m_start;
    TextCursor m_end;
    QString    m_removedText;  // saved on first redo for undo replay
    TextCursor m_cursorBefore;
    TextCursor* m_viewCursor;
    TextCursor* m_viewAnchor;
};

/// A new text set by changing only the lines that differ
/// (SimpleTextDocument::setText() with a line comparison). Created after the
/// document has already changed: the first redo() does nothing.
class ReplaceLinesCommand : public QUndoCommand {
public:
    struct Run {
        int first = 0;
        QStringList removed;
        QStringList inserted;
    };
    /// `runs` in the order they were made (from the last one up).
    /// `beginBatch` and `endBatch` (the view's) wrap each redo and undo, so
    /// that the view follows the many runs once.
    ReplaceLinesCommand(ITextDocument* doc, QVector<Run> runs,
                        TextCursor cursorBefore, TextCursor anchorBefore,
                        TextCursor cursorAfter, TextCursor anchorAfter,
                        TextCursor* viewCursor, TextCursor* viewAnchor,
                        std::function<void()> beginBatch = {}, std::function<void()> endBatch = {});

    void redo() override;
    void undo() override;

private:
    ITextDocument* m_doc;
    QVector<Run> m_runs;
    TextCursor m_cursorBefore, m_anchorBefore, m_cursorAfter, m_anchorAfter;
    TextCursor* m_viewCursor;
    TextCursor* m_viewAnchor;
    std::function<void()> m_beginBatch, m_endBatch;
    bool m_done = true;
};

} // namespace qce
