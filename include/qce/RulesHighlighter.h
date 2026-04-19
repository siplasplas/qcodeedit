#pragma once

#include "IHighlighter.h"

#include <QChar>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVector>

namespace qce {

/// One match rule inside a HighlightContext. Multiple kinds of matches are
/// unified under one struct; fields unused by a given Kind are ignored.
/// Design follows Kate's rule taxonomy closely.
struct HighlightRule {
    enum Kind {
        DetectChar,       ///< single QChar equal to `ch`
        Detect2Chars,     ///< two consecutive QChars: `ch`, `ch1`
        AnyChar,          ///< single QChar that is in `str`
        StringDetect,     ///< exact QString match of `str`
        WordDetect,       ///< StringDetect with word boundaries
        RegExpr,          ///< QRegularExpression `regex`
        Keyword,          ///< word matches a name in keyword list `keywordListId`
        DetectSpaces,     ///< one or more whitespace chars
        DetectIdentifier, ///< [a-zA-Z_][a-zA-Z0-9_]*
        Int,              ///< decimal integer literal
        Float,            ///< floating-point literal
        HlCStringChar,    ///< C escape: \n, \t, \x41, \u0041, ...
        HlCChar,          ///< C character literal: 'x' or '\n'
        HlCOct,           ///< C octal integer literal: 0777
        HlCHex,           ///< C hexadecimal integer literal: 0xFF
        LineContinue,     ///< backslash (or custom char) at end-of-line
        RangeDetect,      ///< from `ch` to `ch1` on the same line
        IncludeRules      ///< try rules of another context
    };
    Kind kind = DetectChar;

    // Kind-specific parameters (only those relevant to `kind` are used):
    QChar   ch;
    QChar   ch1;
    QString str;
    bool    caseSensitive  = true;
    QRegularExpression regex;
    int     keywordListId     = -1;  // for Kind::Keyword
    int     includedContextId = -1;  // for Kind::IncludeRules

    // Common to all rules:
    int  attributeId   = -1;  ///< -1 = use context's defaultAttribute
    int  nextContextId = -1;  ///< -1 = #stay; else push this context
    int  popCount      = 0;   ///< 0,1,2,... for #pop / #pop#pop
    bool lookAhead     = false;
    bool firstNonSpace = false;

    // Folding markers (Kate's beginRegion / endRegion). A single rule may
    // open AND close a region in one step (e.g. C preprocessor 'elif').
    int  beginRegionId = -1;
    int  endRegionId   = -1;

    /// Only match when `pos == column` (Kate's `column` attribute). -1 = any.
    int  column = -1;
};

/// A named state in the highlighter's automaton. Rules are tried in order
/// until one matches; the first match consumes characters and may switch
/// to another context.
struct HighlightContext {
    QString name;
    int     defaultAttribute    = -1;  ///< attribute for unmatched characters
    int     lineEndNextContext  = -1;  ///< -1 = #stay at end of line
    int     lineEndPopCount     = 0;   ///< #pop count before line-end push
    bool    fallthrough             = false;
    int     fallthroughContext      = -1;
    int     fallthroughPopCount     = 0;
    QVector<HighlightRule> rules;
};

/// Named set of words matched by Kind::Keyword rules.
struct KeywordList {
    QString      name;
    QSet<QString> words;
    bool         caseSensitive = true;
};

/// Fold-marker event produced by highlightLineEx(). One per rule match that
/// carries a beginRegion or endRegion attribute. A single rule may produce
/// two events in one step (end then begin, in XML order).
struct FoldMarker {
    int  column;    ///< QChar column in the line where the token starts
    int  length;    ///< length of the matched token (for end-column math)
    int  regionId;  ///< region id from RulesHighlighter::regionIdForName()
    bool isBegin;   ///< true = opens region; false = closes
};

/// Rule-based implementation of IHighlighter. Configured by the caller
/// through the builder API (add*) — the editor library itself never reads
/// XML or any other file format; that's the job of external code (demo).
class RulesHighlighter : public IHighlighter {
public:
    RulesHighlighter() = default;

    // --- Builder API (used by the XML reader / test code) ---------------

    int addAttribute(TextAttribute attr);
    int addContext(HighlightContext ctx);
    int addKeywordList(KeywordList kw);

    /// Lookup/insert: returns a stable id for a fold-region name ("Brace1",
    /// "Comment", "curly", ...). Identical names share the same id so that
    /// endRegion="curly" matches beginRegion="curly".
    int regionIdForName(const QString& name);
    QString regionNameById(int id) const;
    int regionCount() const { return m_regionNames.size(); }

    /// Mutable access to a context, used to append rules after creation
    /// (rules may reference contexts that are defined later).
    HighlightContext& contextRef(int id);

    void setInitialContextId(int id) { m_initialContextId = id; }
    int  initialContextId() const { return m_initialContextId; }

    // --- Name-based lookup (convenience for readers that parse by name) -

    int contextIdByName(const QString& name) const;
    int keywordListIdByName(const QString& name) const;

    // --- IHighlighter ---------------------------------------------------

    HighlightState initialState() const override;
    void highlightLine(const QString&        line,
                       const HighlightState& stateIn,
                       QVector<StyleSpan>&   spans,
                       HighlightState&       stateOut) const override;
    const QVector<TextAttribute>& attributes() const override { return m_attributes; }

    /// Extended variant that also reports fold-marker events encountered
    /// during matching. Used by RuleBasedFoldingProvider. Emits events in
    /// the order rules produced them; when a single rule carries both begin
    /// and end markers, the end event comes first (matches Kate semantics
    /// of 'elif': close the previous #if, then open a new one).
    void highlightLineEx(const QString&        line,
                          const HighlightState& stateIn,
                          QVector<StyleSpan>&   spans,
                          HighlightState&       stateOut,
                          QVector<FoldMarker>&  folds) const;

private:
    /// Try to match `rule` starting at `pos` in `line`. Returns the number of
    /// QChars matched (0 means no match). lookAhead does NOT affect the
    /// returned length — it is handled by the caller.
    int matchAt(const HighlightRule& rule, const QString& line, int pos,
                const QString& identifierChars) const;

    /// Emit a span, merging with the previous one if the attribute is the
    /// same and the ranges are adjacent.
    static void emitSpan(QVector<StyleSpan>& spans, int start, int length, int attrId);

    /// Returns the index of the first non-whitespace QChar in `line`, or -1.
    static int firstNonSpacePos(const QString& line);

    QVector<TextAttribute>     m_attributes;
    QVector<HighlightContext>  m_contexts;
    QVector<KeywordList>       m_keywords;
    QHash<QString, int>        m_contextByName;
    QHash<QString, int>        m_keywordByName;
    QVector<QString>           m_regionNames;   ///< id → name
    QHash<QString, int>        m_regionIdByName;
    int                        m_initialContextId = 0;
};

} // namespace qce
