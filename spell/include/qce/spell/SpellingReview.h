#pragma once

#include <qce/spell/CodeEditSpelling.h>

#include <QDialog>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace qce::spell {

/// What the review window checks when it opens.
enum class CheckedAtFirst {
    Sure,               ///< a single suggestion close to the word (SpellChecker::isConfident())
    Single,             ///< a single suggestion, close or not
    Every,              ///< every word with its first correction
    SeveralFirstClose,  ///< only the words with several suggestions, the first close (SpellChecker::isClose())
    Several,            ///< only the words with several suggestions
    NoSuggestions,      ///< only the words without suggestions given the user's own correction
    None,               ///< nothing: every word, to check by hand
};
/// "sure", "single", "every", "severalFirstClose", "several", "noSuggestions",
/// "none", e.g. for an application's settings.
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
        double score = -1;    ///< lower: closer; -1 for a moved space (no score)
    };
    Range word;
    QString lineText;
    QList<Choice> choices;    ///< best first; a moved space after the word's own suggestions
    bool confident = false;   ///< a single close suggestion and no moved space
    bool single = false;      ///< a single suggestion and no moved space, close or not
    bool firstClose = false;  ///< the first suggestion is close (SpellChecker::isClose())
    bool noSuggestions = false; ///< found without suggestions (it may get the user's own)
    double score = -1;        ///< the first choice's score (lower: closer); -1 without one
};

/// The misspelled words of a text or a selection, each with a correction to
/// choose, the chosen correction's score (how far it is: the lower, the
/// closer), line and
/// context, sorted as in the text, alphabetically or by score. Checked at
/// first as CheckedAtFirst says (the user may change it); the list shows the
/// words of that kind: all, the checked ones (to look them over) or the
/// unchecked ones (left to decide), as "Show" says, or every word. "Show" is
/// applied when chosen, so a word checked or unchecked meanwhile stays in
/// sight. The kinds of words are chosen in
/// "Checked at first" (with several suggestions, or without suggestions but
/// given an own correction, too: to look them over with "Show: Checked
/// words"). Only the check boxes decide which
/// words are corrected, unless "Picking a correction checks the word" is on:
/// then picking one, also the one shown, checks the word. With `spelling`, "Add to Dictionary" and "Ignore" take the
/// selected word out of the list, every occurrence of it ("careta" written on
/// purpose); "Remember Correction" keeps the selected word's chosen
/// correction as the user's own (score 0 from now on, every row of it
/// checked); "Own Correction" asks for the user's own one and keeps it the
/// same way, also for a word without suggestions; "User Dictionary" shows the user's words and corrections. Ctrl+C
/// or the context menu copies the selected row, separated by tabs, or its
/// line of text. One row is selected at a time; Up and Down move between
/// rows, Alt+Down opens the row's corrections. Correct accepts; chosenReplacements()
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

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// openSpellingReview()'s window closed: how many words were corrected,
    /// and the user's last choice of what is checked at first.
    void reviewFinished(int corrected, qce::spell::CheckedAtFirst checkedAtFirst);

private:
    void updateSummary();
    void checkAtFirst();
    void filter();
    void showCurrent();
    /// The item is of the kind (what that choice of "Checked at first" checks).
    static bool ofKind(const SpellingReviewItem& item, CheckedAtFirst kind);
    /// Adds the selected word to the dictionary (or ignores it) and removes
    /// its rows.
    void acceptWord(bool addToDictionary);
    /// Keeps the selected word's chosen correction as the user's own
    /// (SpellChecker::addCorrection()): every row of the word gets it, score
    /// 0, checked.
    void rememberCorrection();
    /// Asks for the user's own correction of the selected word (starting from
    /// the word as typed) and keeps it like rememberCorrection(), also for a
    /// word without suggestions; or, with the words before and after it,
    /// replaces those three here only (not remembered). Ctrl+F6.
    void ownCorrection();
    /// Ignores in the document (its file) the words of `text` that neither the
    /// dictionaries nor the user's words know.
    void ignoreUnknownWords(const QString& text);
    /// The box of a row's corrections (m_items[index].choices).
    void addChoiceBox(QTreeWidgetItem* row, int index);
    /// The selected rows to the clipboard, one a line: word, correction,
    /// score, line and the whole line of text separated by tabs, or only
    /// the line of text.
    void copyRows(bool contextOnly) const;
    /// The score shown in a row (and its sort key); none for a negative one.
    static void showScore(QTreeWidgetItem* row, double score);

    QList<SpellingReviewItem> m_items;
    QTreeWidget* m_list = nullptr;
    QLabel* m_summary = nullptr;
    QComboBox* m_checkAtFirst = nullptr;
    QComboBox* m_show = nullptr;
    QCheckBox* m_pickChecks = nullptr;
    QLabel* m_searchTime = nullptr;
    QPointer<CodeEditSpelling> m_spelling;
    std::function<void()> m_updateButtons;
    bool m_bulkChange = false;  ///< many rows change: the summary waits
    int m_rowHeight = 0;        ///< of every row: a correction box fits
};

/// Reviews the misspelled words of the selection of the spelling's editor, or
/// of its whole text: finds their corrections (with progress for a long
/// text), shows SpellingReviewDialog and replaces the checked words as one
/// undo step. `checkedAtFirst` is the choice to start with and, afterwards,
/// the user's last one (for the application to save). Says so in a message
/// box when there is no misspelled word or checking is off. Returns the
/// number of corrected words.
int reviewSpelling(CodeEditSpelling& spelling, CheckedAtFirst& checkedAtFirst, QWidget* parent);

/// The same in a window that is not modal: the rest of the program goes on
/// while it is open, and the editor whose text is reviewed is read-only until
/// it closes (its earlier state then comes back). Correct replaces the
/// checked words still where they were found; reviewFinished() tells the
/// outcome. Returns the window (deleted when closed; parent it to what owns
/// `spelling`), or null when there was nothing to review (the user was told).
SpellingReviewDialog* openSpellingReview(CodeEditSpelling& spelling, CheckedAtFirst checkedAtFirst, QWidget* parent);

} // namespace qce::spell
