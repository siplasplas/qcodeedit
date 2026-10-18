#include "qce/ITextDocument.h"

namespace qce {

int ITextDocument::maxLineLength() const {
    // Default fallback implementation: linear scan.
    // Backends with cached state should override for O(1).
    int maxLen = 0;
    const int n = lineCount();
    for (int i = 0; i < n; ++i) {
        const int len = lineAt(i).size();
        if (len > maxLen) {
            maxLen = len;
        }
    }
    return maxLen;
}

void ITextDocument::replaceLines(int first, int count, const QStringList& lines) {
    const int total = lineCount();
    const auto lineEnd = [this](int line) { return TextCursor{line, int(lineAt(line).size())}; };
    const QString text = lines.join(QLatin1Char('\n'));
    if (count > 0 && !lines.isEmpty()) {
        removeText({first, 0}, lineEnd(first + count - 1));
        insertText({first, 0}, text);
    } else if (count > 0) {
        // The line break that separated them goes with them.
        if (first + count < total)
            removeText({first, 0}, {first + count, 0});
        else if (first > 0)
            removeText(lineEnd(first - 1), lineEnd(first + count - 1));
        else
            removeText({0, 0}, lineEnd(first + count - 1));
    } else if (!lines.isEmpty()) {
        if (first < total)
            insertText({first, 0}, text + QLatin1Char('\n'));
        else
            insertText(lineEnd(total - 1), QLatin1Char('\n') + text);
    }
}

void ITextDocument::stripTrailingWhitespace() {
    const int n = lineCount();
    for (int i = 0; i < n; ++i) {
        const QString line = lineAt(i);
        int end = line.size();
        while (end > 0 && line.at(end - 1).isSpace()) {
            --end;
        }
        if (end < line.size()) {
            removeText({i, end}, {i, int(line.size())});
        }
    }
}

} // namespace qce
