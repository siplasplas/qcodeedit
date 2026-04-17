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

void ITextDocument::stripTrailingWhitespace() {
    const int n = lineCount();
    for (int i = 0; i < n; ++i) {
        const QString line = lineAt(i);
        int end = line.size();
        while (end > 0 && line.at(end - 1).isSpace()) {
            --end;
        }
        if (end < line.size()) {
            removeText({i, end}, {i, line.size()});
        }
    }
}

} // namespace qce
