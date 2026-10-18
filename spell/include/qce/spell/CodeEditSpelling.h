#pragma once

#include <qce/TextCursor.h>
#include <qce/spell/SpellChecker.h>

#include <QColor>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

class QTimer;

namespace qce {
class CodeEditArea;
class ITextDocument;
}

namespace qce::spell {

/// Spell checking of a qcodeedit editor: underlines misspelled words with a
/// wavy line (in CodeEditArea::SpellingLayer), checking the visible lines
/// right after edits and shortly after scrolling, and only the parts that may
/// be checked (CodeEditArea::spellCheckRanges(): comments, strings and
/// documentation in code, all of plain text). The word being typed is
/// checked once a space or punctuation ends it or the cursor leaves it. When
/// most words are wrong (at least 5 checked) and most of those are right in
/// another installed language, the text is taken to be in that language and
/// nothing is underlined, e.g. before the application has detected the
/// language of a new or pasted text; wrong words no dictionary knows stay
/// underlined, however many. Gives the application what its context menu
/// needs: the misspelled word under the cursor, suggestions, replacing the
/// word as one undo step, adding to the dictionary and ignoring.
class CodeEditSpelling : public QObject {
    Q_OBJECT
public:
    explicit CodeEditSpelling(CodeEditArea* area, QObject* parent = nullptr);
    ~CodeEditSpelling() override;

    /// Dictionaries or language codes, e.g. {"pl"} or {"en", "pl"}.
    void setLanguages(const QStringList& languages);
    QStringList dictionaries() const { return m_checker.dictionaries(); }
    /// At least one dictionary is in use.
    bool isReady() const { return m_checker.isReady(); }

    /// On by default; off removes the underlines.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    /// Off by default. On: a misspelled word with a single correction (one
    /// suggestion, or a moved space ending at it: "mam yz" → "mamy z") is
    /// corrected when a space or punctuation is typed right after it; not in
    /// pasted text, not a word right in another installed language. The
    /// correction is its own undo step: undo brings back the word as typed.
    void setCorrectWhileTyping(bool correct);
    bool correctsWhileTyping() const { return m_correctWhileTyping; }

    void setUnderlineColor(const QColor& color);
    QColor underlineColor() const { return m_color; }

    struct Misspelling {
        TextCursor start;
        TextCursor end;
        QString word;
    };
    /// The misspelled word at `position` (in or right after it), if any and
    /// where checking applies.
    std::optional<Misspelling> misspelledWordAt(TextCursor position);
    /// Suggestions for a word, up to `maxCount`.
    QStringList suggestions(const QString& word, int maxCount = 8) const {
        return m_checker.suggestions(word, maxCount);
    }
    /// Replaces a misspelled word as one undo step.
    void replaceWord(const Misspelling& misspelling, const QString& replacement);
    /// All misspelled words in [from, to), in order, where checking applies
    /// (e.g. for reviewing the whole text or the selection); unlike the
    /// underlines, also when most of them are right in another language.
    QList<Misspelling> misspelledWords(TextCursor from, TextCursor to);
    /// Replaces several words (or phrases), each with its replacement, as one
    /// undo step; a range overlapping one earlier in the list is skipped.
    void replaceWords(const QList<QPair<Misspelling, QString>>& replacements);
    /// Corrections of a space missing or in the wrong place: the misspelled
    /// word joined with the word before or after it on its line (also one
    /// letter long) and split into two right words ("n aoknie" → "na
    /// oknie", "mam yz" → "mamy z"). Each covers both words, in `word` the
    /// text it replaces; pass it to replaceWord() with its suggestion. At
    /// most `maxCount`, the space moved least first.
    struct PhraseCorrection {
        Misspelling range;
        QString replacement;
    };
    QList<PhraseCorrection> phraseCorrections(const Misspelling& misspelling, int maxCount = 3) const;
    /// Adds the word to the shared user dictionary and checks again.
    void addToDictionary(const QString& word);
    /// Accepts the word in this editor (for the session) and checks again.
    void ignore(const QString& word);

    /// Checks the visible lines now (normally done by a timer).
    void recheck();

signals:
    /// A check of the visible lines finished.
    void checked();
    /// A word was corrected while typing (e.g. for the status bar).
    void corrected(const QString& typed, const QString& correction);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void correctTypedWord(TextCursor at, QChar typed);
    void scheduleCheck();
    void onCursorMoved(TextCursor position);

    QPointer<CodeEditArea> m_area;
    QPointer<ITextDocument> m_document;  // whose changes trigger a check
    SpellChecker m_checker;
    /// Whether a word is right in an installed language not in use (to tell
    /// a text in another language from one full of mistakes).
    bool inOtherLanguage(const QString& word);
    std::unique_ptr<SpellChecker> m_otherLanguages;
    QStringList m_otherLanguagesFor;  ///< the dictionaries in use it was made for
    QTimer* m_timer = nullptr;
    QColor m_color{Qt::red};
    bool m_enabled = true;
    bool m_correctWhileTyping = false;
    // The text changed since the last check.
    bool m_edited = false;
    // The word being typed (the one at the cursor after an edit), not
    // underlined until the cursor leaves it; line -1 when none.
    struct TypedWord {
        int line = -1;
        int start = 0;
        int end = 0;
    } m_typedWord;
};

} // namespace qce::spell
