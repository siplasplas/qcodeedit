#pragma once

#include <QList>
#include <QString>

namespace qce::spell {

/// A grammar mistake in a piece of text: the words in question as [start,
/// end) columns, the rule it breaks, and replacements that mend it (a word
/// one typing slip away: "ze jest" → "że jest").
struct GrammarIssue {
    int start = 0;
    int end = 0;
    QString rule;     ///< a short name, e.g. "government", "agreement"
    QString message;  ///< for the user; may be empty
    struct Replacement {
        int start = 0;  ///< the columns of the word replaced
        int end = 0;
        QString text;
    };
    QList<Replacement> replacements;
};

/// Grammar checking a program gives CodeEditSpelling (setGrammarChecker()):
/// an interface only, so that qcodeedit depends on no grammar library; a
/// checker comes from elsewhere (e.g. spellmorf's adapter). Used in the GUI
/// thread.
class GrammarChecker {
public:
    virtual ~GrammarChecker() = default;
    /// The language it checks, as a code ("pl"); used when a dictionary of
    /// that language is in use.
    virtual QString language() const = 0;
    /// The mistakes in `text` (a line, or the part of it that is checked),
    /// columns into `text`. CodeEditSpelling keeps only those whose words are
    /// all spelled right: spelling comes first.
    virtual QList<GrammarIssue> check(const QString& text) const = 0;
};

} // namespace qce::spell
