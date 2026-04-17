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

} // namespace qce
