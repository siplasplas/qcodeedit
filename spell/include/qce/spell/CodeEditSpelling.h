#pragma once

#include <qce/TextCursor.h>
#include <qce/spell/SpellChecker.h>

#include <QColor>
#include <QObject>
#include <QPointer>

#include <optional>

class QTimer;

namespace qce {
class CodeEditArea;
class ITextDocument;
}

namespace qce::spell {

/// Spell checking of a qcodeedit editor: underlines misspelled words with a
/// wavy line (in CodeEditArea::SpellingLayer), checking the visible lines
/// shortly after edits and scrolling, and only the parts that may be checked
/// (CodeEditArea::spellCheckRanges(): comments, strings and documentation in
/// code, all of plain text). Gives the application what its context menu
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
    /// Adds the word to the shared user dictionary and checks again.
    void addToDictionary(const QString& word);
    /// Accepts the word in this editor (for the session) and checks again.
    void ignore(const QString& word);

    /// Checks the visible lines now (normally done by a timer).
    void recheck();

signals:
    /// A check of the visible lines finished.
    void checked();

private:
    void scheduleCheck();

    QPointer<CodeEditArea> m_area;
    QPointer<ITextDocument> m_document;  // whose changes trigger a check
    SpellChecker m_checker;
    QTimer* m_timer = nullptr;
    QColor m_color{Qt::red};
    bool m_enabled = true;
};

} // namespace qce::spell
