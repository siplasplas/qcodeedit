#pragma once

#include <qce/TextCursor.h>

#include <QUndoCommand>
#include <QString>

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

} // namespace qce
