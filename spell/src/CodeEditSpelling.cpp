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
// With at least this many words checked, more than half of them wrong means
// the text is in another language, and nothing is underlined.
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
    // Most words wrong: the text is in another language than the dictionaries
    // (e.g. before the application has detected it), not full of mistakes.
    if (words >= kLanguageWords && underlines.size() * 2 > words) underlines.clear();
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

void CodeEditSpelling::addToDictionary(const QString& word) {
    m_checker.addToDictionary(word);
    recheck();
}

void CodeEditSpelling::ignore(const QString& word) {
    m_checker.ignore(word);
    recheck();
}

} // namespace qce::spell
