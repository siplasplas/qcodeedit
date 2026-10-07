#pragma once

#include "FoldMarker.h"
#include "IHighlighter.h"

#include <QChar>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace qce {

/// Shape classification for RegExpr rules. Most Kate syntax files lean on
/// a handful of trivial patterns (`\s`, `\S`, `[0-9]+`, identifiers, pure
/// literals); matching them directly is an order of magnitude faster than
/// going through pcre2. Rules whose pattern doesn't fit any bucket stay
/// on the pcre path with `Any`.
///
/// Populated once at rule-construction time by classifyRegexShape();
/// matchAt()'s RegExpr arm dispatches on it. Dynamic rules (where the
/// pattern is rebuilt per-match from %n expansion) always take the pcre
/// path — their Shape field is ignored.
enum class RegexShape : int {
    Any = 0,               ///< no fast path — run pcre2
    SingleWhitespace,      ///< `\s`
    WhitespacePlus,        ///< `\s+`
    SingleNonWhitespace,   ///< `\S`
    NonWhitespacePlus,     ///< `\S+`
    Identifier,            ///< `[a-zA-Z_][a-zA-Z0-9_]*` or `[a-zA-Z_]\w*`
    Digits,                ///< `[0-9]+`
    HexDigits,             ///< `[0-9a-fA-F]+` (and case-equivalents)
    Literal,               ///< no meta characters — StringDetect semantics
};

/// Classify a regex source string into a Shape bucket. Purely
/// string-based: exact equality to known patterns (plus a meta-char
/// scan for Literal). Unknown patterns return Any.
RegexShape classifyRegexShape(const QString& pattern);

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

    /// Kate's dynamic="true" on StringDetect / RegExpr: expand %0..%9
    /// placeholders in `str` against the captures that pushed the current
    /// context (HighlightState::captureStack.last()) before matching.
    /// %0 = whole match of the triggering regex; %1..%9 = capture groups.
    bool dynamic = false;

    /// For RegExpr rules: shape of the regex pattern. Readers set this
    /// after building `regex`; matchAt() dispatches to a fast path when
    /// the shape is non-Any. Default Any routes through pcre2 as before.
    RegexShape regexShape = RegexShape::Any;

    /// Kate's multi-push switch "A!B!C": contexts pushed, in order, after
    /// the pops and before nextContextId (here A and B; nextContextId = C).
    /// Kept last so positional (aggregate) initialisation stays valid.
    QVector<int> extraPushContextIds;
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
    // Multi-push switches (see HighlightRule::extraPushContextIds); kept last
    // so positional (aggregate) initialisation stays valid.
    QVector<int> lineEndExtraPushContextIds;
    QVector<int> fallthroughExtraPushContextIds;
};

/// Named set of words matched by Kind::Keyword rules.
struct KeywordList {
    QString      name;
    QSet<QString> words;
    bool         caseSensitive = true;
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

    /// Same as highlightLineEx(); lets CodeEditArea collect fold markers
    /// while highlighting.
    void highlightLineWithFolds(const QString&        line,
                                const HighlightState& stateIn,
                                QVector<StyleSpan>&   spans,
                                HighlightState&       stateOut,
                                QVector<FoldMarker>&  folds) const override {
        highlightLineEx(line, stateIn, spans, stateOut, folds);
    }

private:
    /// Try to match `rule` starting at `pos` in `line`. Returns the number of
    /// QChars matched (0 means no match). lookAhead does NOT affect the
    /// returned length — it is handled by the caller.
    ///
    /// activeCaptures carries the captures of the rule that pushed the
    /// current context (HighlightState::captureStack.last()); used to
    /// expand %n in dynamic StringDetect / RegExpr patterns.
    ///
    /// outCaptures (if non-null) receives regex capture groups on RegExpr
    /// matches (index 0 = whole match, 1..N = capture groups). For
    /// non-regex rules the list is cleared. Callers use this to push the
    /// captures alongside the new context onto HighlightState::captureStack.
    int matchAt(const HighlightRule& rule, const QString& line, int pos,
                const QStringList& activeCaptures,
                QStringList* outCaptures) const;

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
