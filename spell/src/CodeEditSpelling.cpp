#include <qce/spell/CodeEditSpelling.h>

#include <qce/CodeEditArea.h>
#include <qce/ITextDocument.h>
#include <qce/ViewportState.h>

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

// Replaces a range as one undo step and leaves the cursor after the new text.
class ReplaceWordCommand : public QUndoCommand {
public:
    ReplaceWordCommand(CodeEditArea* area, TextCursor start, TextCursor end, QString replacement)
        : m_area(area), m_doc(area->document()), m_start(start), m_end(end),
          m_replacement(std::move(replacement)), m_cursor(area->cursorPosition()) {
        setText(QObject::tr("Correct Spelling"));
    }
    void redo() override {
        if (!m_doc || !m_area) return;
        m_original = m_doc->removeText(m_start, m_end);
        m_newEnd = m_doc->insertText(m_start, m_replacement);
        m_area->setSelection(m_newEnd, m_newEnd);
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
};

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
    scheduleCheck();
}

CodeEditSpelling::~CodeEditSpelling() {
    if (m_area) m_area->setExtraSelections(CodeEditArea::SpellingLayer, {});
}

void CodeEditSpelling::setLanguages(const QStringList& languages) {
    m_checker.setLanguages(languages);
    scheduleCheck();
}

void CodeEditSpelling::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled && m_area) m_area->setExtraSelections(CodeEditArea::SpellingLayer, {});
    scheduleCheck();
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
            for (const auto& word : checkableWords(text, range.first, range.second)) {
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
    if (words >= kLanguageWords && underlines.size() * 2 > words) {
        int otherLanguage = 0;
        for (const ExtraSelection& underline : underlines) {
            const QString word = doc->lineAt(underline.start.line)
                                     .mid(underline.start.column, underline.end.column - underline.start.column);
            if (inOtherLanguage(word)) ++otherLanguage;
        }
        if (otherLanguage * 2 > underlines.size()) underlines.clear();
    }
    m_area->setExtraSelections(CodeEditArea::SpellingLayer, underlines);
    emit checked();
}

std::optional<CodeEditSpelling::Misspelling> CodeEditSpelling::misspelledWordAt(TextCursor position) {
    if (!m_area || !m_area->document() || !m_enabled || !m_checker.isReady()) return std::nullopt;
    ITextDocument* doc = m_area->document();
    if (position.line < 0 || position.line >= doc->lineCount()) return std::nullopt;
    const QString text = doc->lineAt(position.line);
    for (const auto& range : m_area->spellCheckRanges(position.line)) {
        if (position.column < range.first || position.column > range.second) continue;
        for (const auto& word : checkableWords(text, range.first, range.second)) {
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
            for (const auto& word : checkableWords(text, range.first, range.second)) {
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
    // From the end, so that a replacement does not move the ranges before it.
    QList<QPair<Misspelling, QString>> ordered = replacements;
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& a, const auto& b) { return b.first.start < a.first.start; });
    QUndoStack* stack = m_area->undoStack();
    stack->beginMacro(QObject::tr("Correct Spelling"));
    TextCursor limit{INT_MAX, INT_MAX};
    for (const auto& [range, replacement] : ordered) {
        if (limit < range.end) continue;  // overlaps the one after it
        stack->push(new ReplaceWordCommand(m_area, range.start, range.end, replacement));
        limit = range.start;
    }
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

void CodeEditSpelling::ignore(const QString& word) {
    m_checker.ignore(word);
    recheck();
}

} // namespace qce::spell
