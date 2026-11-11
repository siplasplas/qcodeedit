#pragma once

#include <memory>

#include <qce/spell/CodeEditSpelling.h>

#include <QDialog>
#include <QList>
#include <QPointer>

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace qce::spell {

/// The grammar mistakes of a text or of the lines of a selection (only those
/// whose words are all spelled right: spelling first), each with the words
/// in question, the problem, a correction to choose (a word one typing slip
/// away), line and context. "Show" lists all of them or those of one rule;
/// nothing is checked at first; Check All and Uncheck All act on the rows
/// shown. Correct corrects the checked rows shown as one undo step and looks
/// for mistakes again (the window stays open); Correct and Close also closes
/// it. A double click shows the words in the text; Ctrl+C or the context menu
/// copies the selected rows. The editor is read-only
/// while the window is open, as in the spelling review.
class GrammarReviewDialog : public QDialog {
    Q_OBJECT
public:
    ~GrammarReviewDialog();
    GrammarReviewDialog(CodeEditSpelling& spelling, int firstLine, int lastLine, bool inSelection,
                        QWidget* parent = nullptr);
    /// Mistakes corrected so far.
    int correctedSoFar() const;
    /// Mistakes found in the lines now.
    int mistakeCount() const;

signals:
    /// openGrammarReview()'s window closed: how many mistakes were corrected.
    void reviewFinished(int corrected);

private:
    /// Finds the mistakes of the lines again and fills the list.
    void collect();
    void filter();
    void updateSummary();
    /// Corrects the checked rows shown (one undo step); then collect().
    void correct();
    /// The selected rows to the clipboard, one a line: words, correction,
    /// problem, line and the whole line of text, separated by tabs.
    void copyRows();

    // The state, in GrammarReview.cpp: fields added there keep the
    // binary interface (the size of this class does not change).
    struct Private;
    std::unique_ptr<Private> d;
};

/// Reviews the grammar mistakes of the selection's lines of the spelling's
/// editor, or of its whole text, in a window that is not modal (the editor
/// read-only until it closes). Says so in a message box when grammar
/// checking is off or there is no mistake; then returns null. The window is
/// deleted when closed; parent it to what owns `spelling`.
GrammarReviewDialog* openGrammarReview(CodeEditSpelling& spelling, QWidget* parent);

} // namespace qce::spell
