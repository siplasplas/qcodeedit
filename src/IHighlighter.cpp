#include "qce/IHighlighter.h"

#include "qce/Utf8Map.h"

#include <QString>

namespace qce {

void IHighlighter::highlightLineWithFolds(const QString&        line,
                                          const HighlightState& stateIn,
                                          QVector<StyleSpan>&   spans,
                                          HighlightState&       stateOut,
                                          QVector<FoldMarker>&  folds) const {
    folds.clear();
    highlightLine(line, stateIn, spans, stateOut);
}

void IHighlighter::tokenizeBytes(const char*           data,
                                  qsizetype             len,
                                  const HighlightState& stateIn,
                                  QVector<StyleSpan>&   spans,
                                  HighlightState&       stateOut) const {
    spans.clear();
    stateOut = stateIn;
    if (len <= 0) return;

    QVector<StyleSpan>  lineSpans;
    Utf8Map             map;

    qsizetype i = 0;
    while (i < len) {
        qsizetype eol = i;
        while (eol < len && data[eol] != '\n') ++eol;

        const qsizetype segLen = eol - i;
        const QString line = QString::fromUtf8(data + i, segLen);

        // Build the QChar↔byte map once per line, then use it to translate
        // every span produced by highlightLine() into byte offsets.
        map.buildFromUtf8(data + i, segLen);

        HighlightState before = stateOut;
        lineSpans.clear();
        highlightLine(line, before, lineSpans, stateOut);

        for (const StyleSpan& s : lineSpans) {
            const qsizetype b0 = map.byteForQChar(s.start);
            const qsizetype b1 = map.byteForQChar(s.start + s.length);
            spans.append(StyleSpan{
                static_cast<int>(i + b0),
                static_cast<int>(b1 - b0),
                s.attributeId,
            });
        }

        // Advance past the '\n', if any.
        i = eol + (eol < len ? 1 : 0);
    }
}

} // namespace qce
