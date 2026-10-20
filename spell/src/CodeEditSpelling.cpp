#include <qce/spell/CodeEditSpelling.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

#include <QKeyEvent>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>

namespace qce::spell {

namespace {

// How far beyond the visible lines words are checked, so that scrolling a
// little shows underlines at once.
constexpr int kMarginLines = 20;
// Checking waits this long after the last edit or scroll.
constexpr int kDelayMs = 150;
// With at least this many words checked, more than half of them wrong, and
// most of those right in another installed language, the text is in that
// language, and nothing is underlined.
constexpr int kLanguageWords = 5;

// Replaces a range as one undo step and leaves the cursor after the new text,
// or (`keepCursor`, a correction while typing) where it was, moved with the
// text after the range.
class ReplaceWordCommand : public QUndoCommand {
public:
    ReplaceWordCommand(CodeEditArea* area, TextCursor start, TextCursor end, QString replacement,
                       bool keepCursor = false)
        : m_area(area), m_doc(area->document()), m_start(start), m_end(end),
          m_replacement(std::move(replacement)), m_cursor(area->cursorPosition()), m_keepCursor(keepCursor) {
        setText(QObject::tr("Correct Spelling"));
    }
    void redo() override {
        if (!m_doc || !m_area) return;
        m_original = m_doc->removeText(m_start, m_end);
        m_newEnd = m_doc->insertText(m_start, m_replacement);
        TextCursor cursor = m_newEnd;
        if (m_keepCursor) {
            cursor = m_cursor;
            if (cursor.line == m_end.line && m_end <= cursor)
                cursor = {m_newEnd.line, m_newEnd.column + (cursor.column - m_end.column)};
        }
        m_area->setSelection(cursor, cursor);
    }
    void undo() override {
        if (!m_doc || !m_area) return;
        m_doc->removeText(m_start, m_newEnd);
        m_doc->insertText(m_start, m_original);
        m_area->setSelection(m_cursor, m_cursor);
    }

private:
    QPointer<CodeEditArea> m_area;
    QPointer<ITextDocument> m_doc;
    TextCursor m_start, m_end, m_newEnd;
    QString m_replacement, m_original;
    TextCursor m_cursor;
    bool m_keepCursor = false;
};

// A typed character that ends a word: a space or punctuation, not one that
// may be inside a word (apostrophe, hyphen, underscore).
bool endsWord(QChar c) {
    if (c.isSpace()) return true;
    if (c == QLatin1Char('\'') || c == QChar(0x2019) || c == QLatin1Char('-') || c == QLatin1Char('_')) return false;
    return c.isPunct();
}

} // namespace

CodeEditSpelling::CodeEditSpelling(CodeEditArea* area, QObject* parent)
    : QObject(parent ? parent : area), m_area(area) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kDelayMs);
    connect(m_timer, &QTimer::timeout, this, &CodeEditSpelling::recheck);
    connect(area, &CodeEditArea::viewportChanged, this, &CodeEditSpelling::scheduleCheck);
    connect(area, &CodeEditArea::cursorPositionChanged, this, &CodeEditSpelling::onCursorMoved);
    // As the area's child (the default) this object outlives the area's own
    // part; it must not touch the area from then on.
    connect(area, &CodeEditArea::aboutToBeDestroyed, this, [this]() {
        m_timer->stop();
        m_area = nullptr;
    });
    // Typed keys, for correcting while typing.
    area->installEventFilter(this);
    scheduleCheck();
}

CodeEditArea* CodeEditSpelling::area() const {
    return m_area;
}

void CodeEditSpelling::setCorrectWhileTyping(bool correct) {
    m_correctWhileTyping = correct;
}

// A space or punctuation typed (not pasted) right after a word: the word is
// corrected once the key has been handled, if it has a single correction.
bool CodeEditSpelling::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && watched == m_area && m_correctWhileTyping && m_enabled
            && !m_area->readOnly() && !m_area->hasSelection()) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const QString text = key->text();
        if (text.size() == 1 && endsWord(text[0])
                && !(key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
            const TextCursor at = m_area->cursorPosition();
            QMetaObject::invokeMethod(this, [this, at, typed = text[0]]() { correctTypedWord(at, typed); },
                                      Qt::QueuedConnection);
        }
    }
    return QObject::eventFilter(watched, event);
}

void CodeEditSpelling::correctTypedWord(TextCursor at, QChar typed) {
    if (!m_area || !m_area->document() || !m_checker.isReady()) return;
    ITextDocument* doc = m_area->document();
    // The key went in where expected, and nothing else since.
    if (at.line >= doc->lineCount() || m_area->cursorPosition() != TextCursor{at.line, at.column + 1}) return;
    const QString line = doc->lineAt(at.line);
    if (at.column >= line.size() || line[at.column] != typed) return;
    const auto word = misspelledWordAt(at);
    if (!word || word->end.column != at.column) return;
    // A word of another installed language is meant, not mistyped.
    if (inOtherLanguage(word->word)) return;
    // Only a single suggestion, close to the word, and no moved space to
    // choose instead: anything less sure is left for the menu.
    const auto suggestions = m_checker.scoredSuggestions(word->word, 2);
    if (!SpellChecker::isConfident(word->word, suggestions) || !phraseCorrections(*word, 1).isEmpty()) return;
    const PhraseCorrection correction{*word, suggestions.front().text};
    m_area->undoStack()->push(new ReplaceWordCommand(m_area, correction.range.start, correction.range.end,
                                                     correction.replacement, true));
    emit corrected(correction.range.word, correction.replacement);
}

CodeEditSpelling::~CodeEditSpelling() {
    if (!m_area) return;
    m_area->setExtraSelections(CodeEditArea::SpellingLayer, {});
    m_area->setExtraSelections(CodeEditArea::GrammarLayer, {});
}

void CodeEditSpelling::setLanguages(const QStringList& languages) {
    m_checker.setLanguages(languages);
    scheduleCheck();
}

void CodeEditSpelling::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled && m_area) {
        m_area->setExtraSelections(CodeEditArea::SpellingLayer, {});
        m_area->setExtraSelections(CodeEditArea::GrammarLayer, {});
    }
    scheduleCheck();
}

void CodeEditSpelling::setGrammarChecker(std::shared_ptr<GrammarChecker> checker) {
    m_grammar = std::move(checker);
    if (!m_grammar && m_area) m_area->setExtraSelections(CodeEditArea::GrammarLayer, {});
    scheduleCheck();
}

void CodeEditSpelling::setGrammarUnderlineColor(const QColor& color) {
    m_grammarColor = color;
    scheduleCheck();
}

bool CodeEditSpelling::grammarActive() const {
    if (!m_grammar || !m_checker.isReady()) return false;
    const QString language = m_grammar->language();
    for (const QString& dictionary : m_checker.dictionaries())
        if (dictionary.section(QLatin1Char('_'), 0, 0).compare(language, Qt::CaseInsensitive) == 0) return true;
    return false;
}

QList<CodeEditSpelling::GrammarMistake> CodeEditSpelling::grammarMistakes(int line) {
    QList<GrammarMistake> result;
    ITextDocument* doc = m_area->document();
    const QString text = doc->lineAt(line);
    for (const auto& range : m_area->spellCheckRanges(line)) {
        for (const GrammarIssue& found : m_grammar->check(text.mid(range.first, range.second - range.first))) {
            GrammarMistake mistake;
            mistake.issue = found;
            mistake.issue.start += range.first;
            mistake.issue.end += range.first;
            if (mistake.issue.start < range.first || mistake.issue.end > range.second
                    || mistake.issue.start >= mistake.issue.end)
                continue;
            // Spelling first: every word of it right.
            bool spelled = true;
            for (const auto& word : m_checker.checkableWords(text, mistake.issue.start, mistake.issue.end))
                spelled = spelled && m_checker.check(text.mid(word.first, word.second));
            if (!spelled) continue;
            mistake.start = {line, mistake.issue.start};
            mistake.end = {line, mistake.issue.end};
            for (GrammarIssue::Replacement& replacement : mistake.issue.replacements) {
                replacement.start += range.first;
                replacement.end += range.first;
                const Misspelling replaced{{line, replacement.start}, {line, replacement.end},
                                           text.mid(replacement.start, replacement.end - replacement.start)};
                mistake.corrections.append({replaced, replacement.text});
            }
            result.append(mistake);
        }
    }
    return result;
}

std::optional<CodeEditSpelling::GrammarMistake> CodeEditSpelling::grammarMistakeAt(TextCursor position) {
    if (!m_area || !m_area->document() || !m_enabled || !grammarActive()) return std::nullopt;
    if (position.line < 0 || position.line >= m_area->document()->lineCount()) return std::nullopt;
    for (const GrammarMistake& mistake : grammarMistakes(position.line))
        if (position.column >= mistake.start.column && position.column <= mistake.end.column) return mistake;
    return std::nullopt;
}

void CodeEditSpelling::setUnderlineColor(const QColor& color) {
    m_color = color;
    scheduleCheck();
}

void CodeEditSpelling::scheduleCheck() {
    // Edits clear all decorations; follow the document the area shows now.
    if (m_area && m_area->document() != m_document) {
        if (m_document) disconnect(m_document, nullptr, this, nullptr);
        m_document = m_area->document();
        if (m_document) {
            for (auto signal : {&ITextDocument::linesInserted, &ITextDocument::linesRemoved,
                                &ITextDocument::linesChanged})
                connect(m_document, signal, this, [this]() {
                    m_edited = true;
                    scheduleCheck();
                });
            // A new text is checked whole: nothing in it is being typed.
            connect(m_document, &ITextDocument::documentReset, this, [this]() {
                m_typedWord = {};
                scheduleCheck();
            });
        }
    }
    if (m_enabled) m_timer->start();
}

// The area clears all decorations on an edit and reports the cursor after
// it; checking at once then, before the next paint, keeps the underlines of
// the other words from blinking with every typed character.
void CodeEditSpelling::onCursorMoved(TextCursor position) {
    if (m_edited) {
        recheck();
    } else if (m_typedWord.line >= 0
               && (position.line != m_typedWord.line || position.column < m_typedWord.start
                   || position.column > m_typedWord.end)) {
        // The cursor left the word being typed: it is finished.
        m_typedWord = {};
        recheck();
    }
}

void CodeEditSpelling::recheck() {
    if (!m_area || !m_area->document() || !m_enabled) return;
    m_timer->stop();
    // The word at the cursor after an edit is being typed: it is checked once
    // a space or punctuation ends it, or the cursor leaves it.
    const bool edited = m_edited;
    m_edited = false;
    const TextCursor cursor = m_area->cursorPosition();
    if (edited) m_typedWord = {};
    if (!m_checker.isReady()) {
        m_area->setExtraSelections(CodeEditArea::SpellingLayer, {});
        m_area->setExtraSelections(CodeEditArea::GrammarLayer, {});
        emit checked();
        return;
    }
    m_checker.refreshUserDictionary();
    ITextDocument* doc = m_area->document();
    const ViewportState state = m_area->viewportState();
    const int first = qMax(0, state.firstVisibleLine - kMarginLines);
    const int last = qMin(doc->lineCount() - 1, qMax(state.lastVisibleLine, state.firstVisibleLine) + kMarginLines);
    QVector<ExtraSelection> underlines;
    int words = 0;
    for (int line = first; line <= last; ++line) {
        const QString text = doc->lineAt(line);
        for (const auto& range : m_area->spellCheckRanges(line)) {
            for (const auto& word : m_checker.checkableWords(text, range.first, range.second)) {
                const int end = word.first + word.second;
                if (edited && line == cursor.line && cursor.column >= word.first && cursor.column <= end)
                    m_typedWord = {line, word.first, end};
                if (line == m_typedWord.line && word.first == m_typedWord.start) continue;
                ++words;
                if (m_checker.check(text.mid(word.first, word.second))) continue;
                ExtraSelection underline;
                underline.start = {line, word.first};
                underline.end = {line, end};
                underline.underline = m_color;
                underlines.append(underline);
            }
        }
    }
    // Most words wrong, and most of those right in another installed language:
    // the text is in that language (e.g. before the application has detected
    // it), not full of mistakes. Wrong words no dictionary knows stay
    // underlined, however many.
    bool otherLanguageText = false;
    if (words >= kLanguageWords && underlines.size() * 2 > words) {
        int otherLanguage = 0;
        for (const ExtraSelection& underline : underlines) {
            const QString word = doc->lineAt(underline.start.line)
                                     .mid(underline.start.column, underline.end.column - underline.start.column);
            if (inOtherLanguage(word)) ++otherLanguage;
        }
        if (otherLanguage * 2 > underlines.size()) {
            underlines.clear();
            otherLanguageText = true;
        }
    }
    m_area->setExtraSelections(CodeEditArea::SpellingLayer, underlines);
    // Grammar, of the same lines; not in the word being typed, and not in a
    // text of another language (no spelling underlines either).
    QVector<ExtraSelection> grammar;
    if (grammarActive() && !otherLanguageText) {
        for (int line = first; line <= last; ++line)
            for (const GrammarMistake& mistake : grammarMistakes(line)) {
                if (line == m_typedWord.line && mistake.start.column <= m_typedWord.end
                        && mistake.end.column >= m_typedWord.start)
                    continue;
                ExtraSelection underline;
                underline.start = mistake.start;
                underline.end = mistake.end;
                underline.underline = m_grammarColor;
                grammar.append(underline);
            }
    }
    m_area->setExtraSelections(CodeEditArea::GrammarLayer, grammar);
    emit checked();
}

std::optional<CodeEditSpelling::Misspelling> CodeEditSpelling::misspelledWordAt(TextCursor position) {
    if (!m_area || !m_area->document() || !m_enabled || !m_checker.isReady()) return std::nullopt;
    ITextDocument* doc = m_area->document();
    if (position.line < 0 || position.line >= doc->lineCount()) return std::nullopt;
    const QString text = doc->lineAt(position.line);
    for (const auto& range : m_area->spellCheckRanges(position.line)) {
        if (position.column < range.first || position.column > range.second) continue;
        for (const auto& word : m_checker.checkableWords(text, range.first, range.second)) {
            if (position.column < word.first || position.column > word.first + word.second) continue;
            const QString found = text.mid(word.first, word.second);
            if (m_checker.check(found)) return std::nullopt;
            return Misspelling{{position.line, word.first}, {position.line, word.first + word.second}, found};
        }
    }
    return std::nullopt;
}

void CodeEditSpelling::replaceWord(const Misspelling& misspelling, const QString& replacement) {
    if (!m_area || !m_area->document() || m_area->readOnly()) return;
    m_area->undoStack()->push(new ReplaceWordCommand(m_area, misspelling.start, misspelling.end, replacement));
}

QList<CodeEditSpelling::Misspelling> CodeEditSpelling::misspelledWords(TextCursor from, TextCursor to) {
    QList<Misspelling> result;
    if (!m_area || !m_area->document() || !m_enabled || !m_checker.isReady()) return result;
    m_checker.refreshUserDictionary();
    ITextDocument* doc = m_area->document();
    const int lastLine = qMin(to.line, doc->lineCount() - 1);
    for (int line = qMax(0, from.line); line <= lastLine; ++line) {
        const QString text = doc->lineAt(line);
        for (const auto& range : m_area->spellCheckRanges(line)) {
            for (const auto& word : m_checker.checkableWords(text, range.first, range.second)) {
                const TextCursor start{line, word.first}, end{line, word.first + word.second};
                if (start < from || to < end) continue;
                const QString found = text.mid(word.first, word.second);
                if (!m_checker.check(found)) result.append({start, end, found});
            }
        }
    }
    return result;
}

void CodeEditSpelling::replaceWords(const QList<QPair<Misspelling, QString>>& replacements) {
    if (!m_area || !m_area->document() || m_area->readOnly() || replacements.isEmpty()) return;
    // The first of overlapping ranges wins.
    QList<QPair<Misspelling, QString>> ordered;
    for (const auto& replacement : replacements) {
        const bool overlaps = std::any_of(ordered.cbegin(), ordered.cend(), [&](const auto& kept) {
            return replacement.first.start < kept.first.end && kept.first.start < replacement.first.end;
        });
        if (!overlaps) ordered.append(replacement);
    }
    // From the end, so that a replacement does not move the ranges before it.
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return b.first.start < a.first.start; });
    QUndoStack* stack = m_area->undoStack();
    stack->beginMacro(QObject::tr("Correct Spelling"));
    for (const auto& [range, replacement] : ordered)
        stack->push(new ReplaceWordCommand(m_area, range.start, range.end, replacement));
    stack->endMacro();
}

QList<CodeEditSpelling::PhraseCorrection> CodeEditSpelling::phraseCorrections(const Misspelling& misspelling,
                                                                              int maxCount) const {
    QList<PhraseCorrection> result;
    if (!m_area || !m_area->document() || !m_enabled || !m_checker.isReady()) return result;
    const int line = misspelling.start.line;
    const QString text = m_area->document()->lineAt(line);
    const auto isLetter = [](QChar c) { return c.isLetter() || c.isMark(); };
    // The neighbouring word: letters after one run of spaces.
    struct Neighbour { int start = -1, end = -1; };
    Neighbour before, after;
    int gapStart = misspelling.start.column;
    while (gapStart > 0 && text[gapStart - 1] == QLatin1Char(' ')) --gapStart;
    if (gapStart < misspelling.start.column && gapStart > 0 && isLetter(text[gapStart - 1])) {
        before.end = gapStart;
        before.start = gapStart;
        while (before.start > 0 && isLetter(text[before.start - 1])) --before.start;
    }
    int gapEnd = misspelling.end.column;
    while (gapEnd < text.size() && text[gapEnd] == QLatin1Char(' ')) ++gapEnd;
    if (gapEnd > misspelling.end.column && gapEnd < text.size() && isLetter(text[gapEnd])) {
        after.start = gapEnd;
        after.end = gapEnd;
        while (after.end < text.size() && isLetter(text[after.end])) ++after.end;
    }
    const auto add = [&](int start, int end, const QString& first, const QString& second) {
        for (const QString& phrase : m_checker.splitSuggestions(first, second, maxCount)) {
            if (int(result.size()) >= maxCount) return;
            result.append({{{line, start}, {line, end}, text.mid(start, end - start)}, phrase});
        }
    };
    if (before.start >= 0)
        add(before.start, misspelling.end.column, text.mid(before.start, before.end - before.start), misspelling.word);
    if (after.start >= 0)
        add(misspelling.start.column, after.end, misspelling.word, text.mid(after.start, after.end - after.start));
    return result;
}

// One dictionary for each installed language not in use, loaded the first
// time it is needed and again when the languages in use change.
bool CodeEditSpelling::inOtherLanguage(const QString& word) {
    const QStringList inUse = m_checker.dictionaries();
    if (!m_otherLanguages || m_otherLanguagesFor != inUse) {
        QSet<QString> usedLanguages;
        QStringList others;
        const QList<Dictionary> installed = installedDictionaries();
        for (const Dictionary& dictionary : installed)
            if (inUse.contains(dictionary.name)) usedLanguages.insert(dictionary.language);
        for (const Dictionary& dictionary : installed) {
            if (usedLanguages.contains(dictionary.language)) continue;
            usedLanguages.insert(dictionary.language);
            others << dictionaryFor(dictionary.language);
        }
        m_otherLanguages = std::make_unique<SpellChecker>();
        m_otherLanguages->setLanguages(others);
        m_otherLanguagesFor = inUse;
    }
    // check() accepts everything when no dictionary is in use.
    return m_otherLanguages->isReady() && m_otherLanguages->check(word);
}

void CodeEditSpelling::addToDictionary(const QString& word) {
    m_checker.addToDictionary(word);
    recheck();
}

void CodeEditSpelling::setDocumentPath(const QString& path) {
    if (path == m_checker.documentPath()) return;
    m_checker.setDocumentPath(path);
    recheck();
}

void CodeEditSpelling::ignore(const QString& word) {
    m_checker.ignore(word);
    recheck();
}

} // namespace qce::spell
