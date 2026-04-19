#include "qce/RulesHighlighter.h"

namespace qce {

// ---------------------------------------------------------------------------
// Builder API
// ---------------------------------------------------------------------------

int RulesHighlighter::addAttribute(TextAttribute attr) {
    m_attributes.push_back(std::move(attr));
    return m_attributes.size() - 1;
}

int RulesHighlighter::addContext(HighlightContext ctx) {
    const QString name = ctx.name;
    m_contexts.push_back(std::move(ctx));
    const int id = m_contexts.size() - 1;
    if (!name.isEmpty()) {
        m_contextByName.insert(name, id);
    }
    return id;
}

int RulesHighlighter::addKeywordList(KeywordList kw) {
    const QString name = kw.name;
    m_keywords.push_back(std::move(kw));
    const int id = m_keywords.size() - 1;
    if (!name.isEmpty()) {
        m_keywordByName.insert(name, id);
    }
    return id;
}

int RulesHighlighter::regionIdForName(const QString& name) {
    if (name.isEmpty()) return -1;
    auto it = m_regionIdByName.find(name);
    if (it != m_regionIdByName.end()) return it.value();
    const int id = m_regionNames.size();
    m_regionNames.push_back(name);
    m_regionIdByName.insert(name, id);
    return id;
}

QString RulesHighlighter::regionNameById(int id) const {
    if (id < 0 || id >= m_regionNames.size()) return {};
    return m_regionNames[id];
}

HighlightContext& RulesHighlighter::contextRef(int id) {
    Q_ASSERT(id >= 0 && id < m_contexts.size());
    return m_contexts[id];
}

int RulesHighlighter::contextIdByName(const QString& name) const {
    return m_contextByName.value(name, -1);
}

int RulesHighlighter::keywordListIdByName(const QString& name) const {
    return m_keywordByName.value(name, -1);
}

// ---------------------------------------------------------------------------
// IHighlighter
// ---------------------------------------------------------------------------

HighlightState RulesHighlighter::initialState() const {
    HighlightState s;
    s.contextStack.push_back(m_initialContextId);
    return s;
}

static bool isIdentifierStart(QChar c) {
    return c.isLetter() || c == QLatin1Char('_');
}

static bool isIdentifierCont(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

/// Returns true if `c` is a "deliminator" character for word-based rules
/// (Keyword, WordDetect). Non-deliminators are identifier characters.
static bool isWordDeliminator(QChar c) {
    return !isIdentifierCont(c);
}

int RulesHighlighter::firstNonSpacePos(const QString& line) {
    for (int i = 0; i < line.size(); ++i) {
        if (!line.at(i).isSpace()) return i;
    }
    return -1;
}

void RulesHighlighter::emitSpan(QVector<StyleSpan>& spans,
                                 int start, int length, int attrId) {
    if (length <= 0) return;
    if (!spans.isEmpty()) {
        StyleSpan& prev = spans.last();
        if (prev.attributeId == attrId && prev.start + prev.length == start) {
            prev.length += length;
            return;
        }
    }
    spans.push_back({start, length, attrId});
}

// ---------------------------------------------------------------------------
// Rule matching
// ---------------------------------------------------------------------------

int RulesHighlighter::matchAt(const HighlightRule& rule,
                               const QString& line, int pos,
                               const QString& /*identifierChars*/) const {
    const int remaining = line.size() - pos;
    if (remaining <= 0) return 0;

    switch (rule.kind) {
    case HighlightRule::DetectChar:
        return (line.at(pos) == rule.ch) ? 1 : 0;

    case HighlightRule::Detect2Chars:
        if (remaining < 2) return 0;
        return (line.at(pos) == rule.ch && line.at(pos + 1) == rule.ch1) ? 2 : 0;

    case HighlightRule::AnyChar:
        return rule.str.contains(line.at(pos)) ? 1 : 0;

    case HighlightRule::StringDetect: {
        const int n = rule.str.size();
        if (remaining < n) return 0;
        const auto cs = rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        return (QStringView(line).mid(pos, n).compare(rule.str, cs) == 0) ? n : 0;
    }

    case HighlightRule::WordDetect: {
        const int n = rule.str.size();
        if (remaining < n) return 0;
        const auto cs = rule.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        if (QStringView(line).mid(pos, n).compare(rule.str, cs) != 0) return 0;
        // word boundaries: char before must be non-identifier (or start),
        // char after must be non-identifier (or end)
        if (pos > 0 && isIdentifierCont(line.at(pos - 1))) return 0;
        if (pos + n < line.size() && isIdentifierCont(line.at(pos + n))) return 0;
        return n;
    }

    case HighlightRule::RegExpr: {
        if (!rule.regex.isValid()) return 0;
        QRegularExpressionMatch m = rule.regex.match(
            line, pos, QRegularExpression::NormalMatch,
            QRegularExpression::AnchorAtOffsetMatchOption);
        if (!m.hasMatch() || m.capturedStart() != pos) return 0;
        return m.capturedLength();
    }

    case HighlightRule::Keyword: {
        if (rule.keywordListId < 0 || rule.keywordListId >= m_keywords.size()) return 0;
        // word boundary before
        if (pos > 0 && isIdentifierCont(line.at(pos - 1))) return 0;
        // scan word
        int end = pos;
        while (end < line.size() && !isWordDeliminator(line.at(end))) ++end;
        if (end == pos) return 0;
        const QString word = line.mid(pos, end - pos);
        const KeywordList& kl = m_keywords[rule.keywordListId];
        if (kl.caseSensitive) {
            return kl.words.contains(word) ? (end - pos) : 0;
        } else {
            // case-insensitive lookup: linear. For large lists consider a
            // second lowercase-set; v1 keeps it simple.
            const QString lower = word.toLower();
            for (const QString& w : kl.words) {
                if (w.toLower() == lower) return end - pos;
            }
            return 0;
        }
    }

    case HighlightRule::DetectSpaces: {
        int end = pos;
        while (end < line.size() && line.at(end).isSpace()) ++end;
        return end - pos;
    }

    case HighlightRule::DetectIdentifier: {
        if (!isIdentifierStart(line.at(pos))) return 0;
        int end = pos + 1;
        while (end < line.size() && isIdentifierCont(line.at(end))) ++end;
        return end - pos;
    }

    case HighlightRule::Int: {
        if (!line.at(pos).isDigit()) return 0;
        int end = pos + 1;
        while (end < line.size() && line.at(end).isDigit()) ++end;
        return end - pos;
    }

    case HighlightRule::Float: {
        int end = pos;
        while (end < line.size() && line.at(end).isDigit()) ++end;
        if (end >= line.size() || line.at(end) != QLatin1Char('.')) return 0;
        ++end;
        if (end >= line.size() || !line.at(end).isDigit()) return 0;
        while (end < line.size() && line.at(end).isDigit()) ++end;
        return end - pos;
    }

    case HighlightRule::HlCChar: {
        // 'x' or '\n', '\xHH', '\NNN' (octal) — returns full length with quotes
        if (line.at(pos) != QLatin1Char('\'')) return 0;
        if (remaining < 3) return 0;
        int end = pos + 1;
        if (line.at(end) == QLatin1Char('\\')) {
            if (end + 1 >= line.size()) return 0;
            const QChar esc = line.at(end + 1);
            static const QString simpleEsc =
                QStringLiteral("ntr0\\'\"\abfv?");
            if (simpleEsc.contains(esc)) {
                end += 2;
            } else if (esc == QLatin1Char('x')) {
                end += 2;
                while (end < line.size()
                       && (line.at(end).isDigit()
                           || (line.at(end) >= QLatin1Char('a') && line.at(end) <= QLatin1Char('f'))
                           || (line.at(end) >= QLatin1Char('A') && line.at(end) <= QLatin1Char('F'))))
                    ++end;
                if (end == pos + 3) return 0; // no hex digits
            } else if (line.at(end) >= QLatin1Char('0') && line.at(end) <= QLatin1Char('7')) {
                end += 1;
                for (int i = 0; i < 2 && end < line.size()
                     && line.at(end) >= QLatin1Char('0')
                     && line.at(end) <= QLatin1Char('7'); ++i)
                    ++end;
            } else {
                return 0;
            }
        } else {
            ++end; // single printable char
        }
        if (end >= line.size() || line.at(end) != QLatin1Char('\'')) return 0;
        return end - pos + 1;
    }

    case HighlightRule::HlCOct: {
        // Octal: 0[0-7]+ (not followed by digit to avoid overlapping with Int)
        if (line.at(pos) != QLatin1Char('0')) return 0;
        int end = pos + 1;
        while (end < line.size()
               && line.at(end) >= QLatin1Char('0')
               && line.at(end) <= QLatin1Char('7'))
            ++end;
        return (end > pos + 1) ? (end - pos) : 0;
    }

    case HighlightRule::HlCHex: {
        // Hex: 0[xX][0-9a-fA-F]+
        if (remaining < 3) return 0;
        if (line.at(pos) != QLatin1Char('0')) return 0;
        const QChar x = line.at(pos + 1);
        if (x != QLatin1Char('x') && x != QLatin1Char('X')) return 0;
        int end = pos + 2;
        while (end < line.size()
               && (line.at(end).isDigit()
                   || (line.at(end) >= QLatin1Char('a') && line.at(end) <= QLatin1Char('f'))
                   || (line.at(end) >= QLatin1Char('A') && line.at(end) <= QLatin1Char('F'))))
            ++end;
        return (end > pos + 2) ? (end - pos) : 0;
    }

    case HighlightRule::HlCStringChar: {
        // \n, \t, \r, \\, \', \", \0, \xHH, \uHHHH, \NNN (octal)
        if (line.at(pos) != QLatin1Char('\\')) return 0;
        if (remaining < 2) return 0;
        const QChar c = line.at(pos + 1);
        if (c == QLatin1Char('n') || c == QLatin1Char('t') || c == QLatin1Char('r')
            || c == QLatin1Char('0') || c == QLatin1Char('\\')
            || c == QLatin1Char('\'') || c == QLatin1Char('"')
            || c == QLatin1Char('a') || c == QLatin1Char('b') || c == QLatin1Char('f')
            || c == QLatin1Char('v')) {
            return 2;
        }
        if (c == QLatin1Char('x')) {
            int end = pos + 2;
            while (end < line.size() && (line.at(end).isDigit()
                || (line.at(end) >= QLatin1Char('a') && line.at(end) <= QLatin1Char('f'))
                || (line.at(end) >= QLatin1Char('A') && line.at(end) <= QLatin1Char('F')))) {
                ++end;
            }
            return (end > pos + 2) ? (end - pos) : 0;
        }
        return 0;
    }

    case HighlightRule::LineContinue: {
        const QChar lc = rule.ch.isNull() ? QLatin1Char('\\') : rule.ch;
        return (line.at(pos) == lc && pos == line.size() - 1) ? 1 : 0;
    }

    case HighlightRule::RangeDetect: {
        if (line.at(pos) != rule.ch) return 0;
        for (int i = pos + 1; i < line.size(); ++i) {
            if (line.at(i) == rule.ch1) return i - pos + 1;
        }
        return 0;
    }

    case HighlightRule::IncludeRules:
        // Handled specially in highlightLine(); matchAt should not be called on it.
        return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// highlightLine
// ---------------------------------------------------------------------------

void RulesHighlighter::highlightLine(const QString&        line,
                                      const HighlightState& stateIn,
                                      QVector<StyleSpan>&   spans,
                                      HighlightState&       stateOut) const {
    QVector<FoldMarker> throwaway;
    highlightLineEx(line, stateIn, spans, stateOut, throwaway);
}

void RulesHighlighter::highlightLineEx(const QString&        line,
                                       const HighlightState& stateIn,
                                       QVector<StyleSpan>&   spans,
                                       HighlightState&       stateOut,
                                       QVector<FoldMarker>&  folds) const {
    spans.clear();
    folds.clear();
    stateOut = stateIn;

    // Guarantee a non-empty stack: if caller passed an empty state, treat it
    // as "start from the initial context".
    if (stateOut.contextStack.isEmpty()) {
        stateOut.contextStack.push_back(m_initialContextId);
    }

    const int firstNonWs = firstNonSpacePos(line);

    int pos = 0;
    int fallthroughDepth = 0;  // guard against infinite fallthrough chains
    while (pos < line.size()) {
        const int ctxId = stateOut.contextStack.last();
        if (ctxId < 0 || ctxId >= m_contexts.size()) break;
        const HighlightContext& ctx = m_contexts[ctxId];

        // Find first matching rule. IncludeRules flattened by iterating
        // included context's rules inline.
        const HighlightRule* matched = nullptr;
        int matchedLen = 0;

        // Try rules of this context, following IncludeRules one level deep
        // (v1 — we flatten at match time; infinite-include detection is the
        // caller's responsibility).
        auto tryContext = [&](const HighlightContext& c, auto& tryRef) -> bool {
            for (const HighlightRule& rule : c.rules) {
                if (rule.kind == HighlightRule::IncludeRules) {
                    if (rule.includedContextId >= 0
                        && rule.includedContextId < m_contexts.size()
                        && rule.includedContextId != ctxId) {
                        if (tryRef(m_contexts[rule.includedContextId], tryRef)) return true;
                    }
                    continue;
                }
                if (rule.firstNonSpace && pos != firstNonWs) continue;
                if (rule.column >= 0 && pos != rule.column) continue;
                const int len = matchAt(rule, line, pos, QString());
                if (len > 0) {
                    matched = &rule;
                    matchedLen = len;
                    return true;
                }
            }
            return false;
        };
        tryContext(ctx, tryContext);

        if (matched) {
            fallthroughDepth = 0;
            const int attr = (matched->attributeId >= 0)
                ? matched->attributeId : ctx.defaultAttribute;
            // End event before begin — matches Kate's 'elif' semantics.
            if (matched->endRegionId >= 0) {
                folds.push_back({pos, matchedLen, matched->endRegionId, false});
            }
            if (matched->beginRegionId >= 0) {
                folds.push_back({pos, matchedLen, matched->beginRegionId, true});
            }
            if (!matched->lookAhead) {
                emitSpan(spans, pos, matchedLen, attr);
                pos += matchedLen;
            }
            // Context switch: pop then push.
            for (int i = 0; i < matched->popCount && stateOut.contextStack.size() > 1; ++i) {
                stateOut.contextStack.pop_back();
            }
            if (matched->nextContextId >= 0) {
                stateOut.contextStack.push_back(matched->nextContextId);
            }
        } else if (ctx.fallthrough
                   && (ctx.fallthroughContext >= 0 || ctx.fallthroughPopCount > 0)
                   && fallthroughDepth < 16) {
            // No rule matched — try the fallthrough context without consuming
            // the character. Guard against infinite chains via depth counter.
            ++fallthroughDepth;
            for (int i = 0; i < ctx.fallthroughPopCount
                 && stateOut.contextStack.size() > 1; ++i)
                stateOut.contextStack.pop_back();
            if (ctx.fallthroughContext >= 0)
                stateOut.contextStack.push_back(ctx.fallthroughContext);
        } else {
            // No rule matched — emit one character with default attribute
            // and advance. This prevents infinite loops.
            fallthroughDepth = 0;
            emitSpan(spans, pos, 1, ctx.defaultAttribute);
            ++pos;
        }
    }

    // End-of-line context switch (applied against whichever context is active
    // at end of line).
    if (!stateOut.contextStack.isEmpty()) {
        const int ctxId = stateOut.contextStack.last();
        if (ctxId >= 0 && ctxId < m_contexts.size()) {
            const HighlightContext& ctx = m_contexts[ctxId];
            for (int i = 0; i < ctx.lineEndPopCount && stateOut.contextStack.size() > 1; ++i) {
                stateOut.contextStack.pop_back();
            }
            if (ctx.lineEndNextContext >= 0) {
                stateOut.contextStack.push_back(ctx.lineEndNextContext);
            }
        }
    }
}

} // namespace qce
