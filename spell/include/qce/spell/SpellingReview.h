#pragma once

#include <qce/spell/CodeEditSpelling.h>

#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;
class QWidget;

namespace qce::spell {

/// What the review window checks when it opens.
enum class CheckedAtFirst {
    Sure,    ///< a single suggestion close to the word (SpellChecker::isConfident())
    Single,  ///< a single suggestion, close or not
    Every,   ///< every word with its first correction
};
/// "sure", "single", "every", e.g. for an application's settings.
QString checkedAtFirstName(CheckedAtFirst checked);
/// The name's value; Sure for an unknown one.
CheckedAtFirst checkedAtFirstFromName(const QString& name);

/// One misspelled word of the text, with its corrections.
struct SpellingReviewItem {
    using Range = CodeEditSpelling::Misspelling;
    struct Choice {
        Range range;          ///< the word, or the word with its neighbour for a moved space
        QString replacement;
        QString label;        ///< as listed ("mam yz → mamy z" for a moved space)
    };
    Range word;
    QString lineText;
    QList<Choice> choices;    ///< best first; a moved space after the word's own suggestions
    bool confident = false;   ///< a single close suggestion and no moved space
    bool single = false;      ///< a single suggestion and no moved space, close or not
    double score = -1;        ///< the first suggestion's score (lower: closer); -1 without one
};

/// The misspelled words of a text or a selection, each with a correction to
/// choose, its score (how far it is: the lower, the closer), line and
/// context, sorted as in the text, alphabetically or by score. Checked at
/// first as CheckedAtFirst says (the user may change it). "Show" lists all
/// words, only the checked ones (to look them over) or only the unchecked
/// ones (left to decide), applied when chosen, so a word checked or
/// unchecked meanwhile stays in sight. Only the check boxes decide which
/// words are corrected, unless "Picking a correction checks the word" is on. With `spelling`, "Add to Dictionary" and "Ignore" take the
/// selected word out of the list, every occurrence of it ("careta" written on
/// purpose). Ctrl+C or the context menu copies the selected rows, separated
/// by tabs, or their lines of text. Correct accepts; chosenReplacements()
/// gives the checked words.
class SpellingReviewDialog : public QDialog {
    Q_OBJECT
public:
    SpellingReviewDialog(const QList<SpellingReviewItem>& items, bool inSelection, CheckedAtFirst checkedAtFirst,
                         QWidget* parent = nullptr, CodeEditSpelling* spelling = nullptr);

    /// The checked words with the chosen corrections.
    QList<QPair<SpellingReviewItem::Range, QString>> chosenReplacements() const;
    /// The user's last choice of what is checked at first.
    CheckedAtFirst checkedAtFirst() const;
    /// How long finding the corrections took, shown with the time a word.
    void setSearchTime(qint64 milliseconds);

private:
    void updateSummary();
    void checkAtFirst();
    void filter();
    /// Adds the selected word to the dictionary (or ignores it) and removes
    /// its rows.
    void acceptWord(bool addToDictionary);
    /// The selected rows to the clipboard, one a line: word, correction,
    /// score, line and the whole line of text separated by tabs, or only
    /// the line of text.
    void copyRows(bool contextOnly) const;

    QList<SpellingReviewItem> m_items;
    QTreeWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QComboBox* m_checkAtFirst = nullptr;
    QComboBox* m_show = nullptr;
    QCheckBox* m_pickChecks = nullptr;
    QLabel* m_searchTime = nullptr;
    CodeEditSpelling* m_spelling = nullptr;
};

/// Reviews the misspelled words of the selection of the spelling's editor, or
/// of its whole text: finds their corrections (with progress for a long
/// text), shows SpellingReviewDialog and replaces the checked words as one
/// undo step. `checkedAtFirst` is the choice to start with and, afterwards,
/// the user's last one (for the application to save). Says so in a message
/// box when there is no misspelled word or checking is off. Returns the
/// number of corrected words.
int reviewSpelling(CodeEditSpelling& spelling, CheckedAtFirst& checkedAtFirst, QWidget* parent);

} // namespace qce::spell
