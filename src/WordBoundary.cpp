#include <qce/WordBoundary.h>

namespace qce::words {

namespace {

enum class CharClass { Space, Word, Punct };

char32_t codePointAt(const QString& text, int index) {
    const QChar c = text.at(index);
    if (c.isHighSurrogate() && index + 1 < text.size() && text.at(index + 1).isLowSurrogate())
        return QChar::surrogateToUcs4(c, text.at(index + 1));
    if (c.isLowSurrogate() && index > 0 && text.at(index - 1).isHighSurrogate())
        return QChar::surrogateToUcs4(text.at(index - 1), c);
    return c.unicode();
}

bool isWordCodePoint(char32_t cp) {
    switch (QChar::category(cp)) {
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
    case QChar::Punctuation_Connector:
        return true;
    default:
        return QChar::isLetterOrNumber(cp);
    }
}

CharClass classAt(const QString& text, int index) {
    const char32_t cp = codePointAt(text, index);
    if (QChar::isSpace(cp)) return CharClass::Space;
    return isWordCodePoint(cp) ? CharClass::Word : CharClass::Punct;
}

} // namespace

bool isWordCharAt(const QString& text, int index) {
    return index >= 0 && index < text.size() && isWordCodePoint(codePointAt(text, index));
}

int nextWordStop(const QString& line, int col) {
    const int len = int(line.size());
    if (col >= len) return len;
    const CharClass cls = classAt(line, col);
    if (cls != CharClass::Space)
        while (col < len && classAt(line, col) == cls) ++col;
    while (col < len && classAt(line, col) == CharClass::Space) ++col;
    return col;
}

int previousWordStop(const QString& line, int col) {
    col = qMin(col, int(line.size()));
    while (col > 0 && classAt(line, col - 1) == CharClass::Space) --col;
    if (col == 0) return 0;
    const CharClass cls = classAt(line, col - 1);
    while (col > 0 && classAt(line, col - 1) == cls) --col;
    return col;
}

bool isWholeWord(const QString& text, int start, int end) {
    return !isWordCharAt(text, start - 1) && !isWordCharAt(text, end);
}

QString wholeWordPattern(const QString& pattern) {
    // \p{..} classes work on code points, matching isWordCharAt().
    static const QString word = QStringLiteral("[\\p{L}\\p{N}\\p{M}\\p{Pc}]");
    return QStringLiteral("(?<!") + word + QStringLiteral(")(?:") + pattern
         + QStringLiteral(")(?!") + word + QStringLiteral(")");
}

} // namespace qce::words
