#include "EditCommands.h"

#include <qce/ITextDocument.h>

namespace qce {

// --- InsertCommand -------------------------------------------------------

InsertCommand::InsertCommand(ITextDocument* doc,
                             TextCursor insertPos,
                             const QString& text,
                             TextCursor cursorBefore,
                             TextCursor* viewCursor,
                             TextCursor* viewAnchor)
    : m_doc(doc),
      m_insertPos(insertPos),
      m_text(text),
      m_endPos(insertPos),
      m_cursorBefore(cursorBefore),
      m_viewCursor(viewCursor),
      m_viewAnchor(viewAnchor) {}

void InsertCommand::redo() {
    m_endPos = m_doc->insertText(m_insertPos, m_text);
    *m_viewCursor = m_endPos;
    *m_viewAnchor = m_endPos;
}

void InsertCommand::undo() {
    m_doc->removeText(m_insertPos, m_endPos);
    *m_viewCursor = m_cursorBefore;
    *m_viewAnchor = m_cursorBefore;
}

bool InsertCommand::mergeWith(const QUndoCommand* other) {
    if (other->id() != id()) {
        return false;
    }
    const auto* o = static_cast<const InsertCommand*>(other);

    // Only merge consecutive single-line (no newline) inserts.
    if (m_text.contains(QLatin1Char('\n')) || o->m_text.contains(QLatin1Char('\n'))) {
        return false;
    }
    if (o->m_insertPos != m_endPos) {
        return false;
    }

    m_text   += o->m_text;
    m_endPos  = o->m_endPos;
    // Keep m_cursorBefore from the first command in the sequence.
    *m_viewCursor = m_endPos;
    *m_viewAnchor = m_endPos;
    return true;
}

// --- RemoveCommand -------------------------------------------------------

RemoveCommand::RemoveCommand(ITextDocument* doc,
                             TextCursor start,
                             TextCursor end,
                             TextCursor cursorBefore,
                             TextCursor* viewCursor,
                             TextCursor* viewAnchor)
    : m_doc(doc),
      m_start(start),
      m_end(end),
      m_cursorBefore(cursorBefore),
      m_viewCursor(viewCursor),
      m_viewAnchor(viewAnchor) {
    // Ensure canonical order.
    if (m_end < m_start) {
        qSwap(m_start, m_end);
    }
}

void RemoveCommand::redo() {
    m_removedText = m_doc->removeText(m_start, m_end);
    *m_viewCursor = m_start;
    *m_viewAnchor = m_start;
}

void RemoveCommand::undo() {
    m_doc->insertText(m_start, m_removedText);
    *m_viewCursor = m_cursorBefore;
    *m_viewAnchor = m_cursorBefore;
}

// --- ReplaceLinesCommand -------------------------------------------------

ReplaceLinesCommand::ReplaceLinesCommand(ITextDocument* doc, QVector<Run> runs,
                                         TextCursor cursorBefore, TextCursor anchorBefore,
                                         TextCursor cursorAfter, TextCursor anchorAfter,
                                         TextCursor* viewCursor, TextCursor* viewAnchor)
    : m_doc(doc),
      m_runs(std::move(runs)),
      m_cursorBefore(cursorBefore),
      m_anchorBefore(anchorBefore),
      m_cursorAfter(cursorAfter),
      m_anchorAfter(anchorAfter),
      m_viewCursor(viewCursor),
      m_viewAnchor(viewAnchor) {}

void ReplaceLinesCommand::redo() {
    if (m_done) {
        // Pushed after the document changed.
        m_done = false;
        return;
    }
    for (const Run& run : m_runs) {
        m_doc->replaceLines(run.first, run.removed.size(), run.inserted);
    }
    *m_viewCursor = m_cursorAfter;
    *m_viewAnchor = m_anchorAfter;
}

void ReplaceLinesCommand::undo() {
    // The run made last is the topmost; its line numbers hold in the new
    // text, and those of the next one once it is undone.
    for (auto it = m_runs.crbegin(); it != m_runs.crend(); ++it) {
        m_doc->replaceLines(it->first, it->inserted.size(), it->removed);
    }
    *m_viewCursor = m_cursorBefore;
    *m_viewAnchor = m_anchorBefore;
}

} // namespace qce
