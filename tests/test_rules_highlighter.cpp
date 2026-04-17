#include <qce/RulesHighlighter.h>

#include <QtTest/QtTest>

using qce::HighlightContext;
using qce::HighlightRule;
using qce::HighlightState;
using qce::KeywordList;
using qce::RulesHighlighter;
using qce::StyleSpan;
using qce::TextAttribute;

class TestRulesHighlighter : public QObject {
    Q_OBJECT
private slots:
    void detectChar_matchesSingleChar();
    void detect2Chars_matchesPair();
    void stringDetect_insideContextGetsDefaultAttr();
    void keyword_matchesWordInList();
    void keyword_doesNotMatchPartialWord();
    void detectIdentifier_matchesAlnum();
    void detectSpaces_consumesWhitespaceRun();
    void anyChar_singleCharInSet();
    void regExpr_numberLiteral();

    void context_stringSpansAreMerged();
    void context_multiLineComment_statePreservesAcrossLines();
    void context_popReturnsToPreviousContext();
    void nothingMatches_emitsDefaultAttrPerChar();
    void initialState_hasOneContextId();
};

// ---------------------------------------------------------------------------
// Tiny helper: build a minimal highlighter and run one line.
// ---------------------------------------------------------------------------
static void runLine(const RulesHighlighter& hl, const QString& line,
                     QVector<StyleSpan>& out) {
    HighlightState st = hl.initialState(), stOut;
    hl.highlightLine(line, st, out, stOut);
}

void TestRulesHighlighter::detectChar_matchesSingleChar() {
    RulesHighlighter hl;
    const int attr = hl.addAttribute({QColor("red")});
    HighlightContext ctx{"Normal", -1, -1, 0, false, -1, {}};
    int ctxId = hl.addContext(ctx);
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attr, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral(" \" "), spans);
    // Spans: default attr at 0 (len 1 space), attr at 1 (the quote), default at 2.
    QCOMPARE(spans.size(), 3);
    QCOMPARE(spans[1].start, 1);
    QCOMPARE(spans[1].length, 1);
    QCOMPARE(spans[1].attributeId, attr);
}

void TestRulesHighlighter::detect2Chars_matchesPair() {
    RulesHighlighter hl;
    const int attr = hl.addAttribute({QColor("green")});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::Detect2Chars, '/', '*', {}, true, {}, -1, -1,
         attr, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("x/*y"), spans);
    // 'x' default, "/*" attr (length 2), 'y' default.
    bool found = false;
    for (const auto& s : spans) {
        if (s.attributeId == attr && s.start == 1 && s.length == 2) found = true;
    }
    QVERIFY(found);
}

void TestRulesHighlighter::stringDetect_insideContextGetsDefaultAttr() {
    RulesHighlighter hl;
    const int attrString = hl.addAttribute({QColor("blue")});
    int ctxNormal = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    int ctxString = hl.addContext({"String", attrString, -1, 0, false, -1, {}});
    hl.contextRef(ctxNormal).rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrString, ctxString, 0, false, false});
    hl.contextRef(ctxString).rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrString, -1, 1, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("\"abc\""), spans);
    // All 5 chars should be attrString (default of String context).
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].length, 5);
    QCOMPARE(spans[0].attributeId, attrString);
}

void TestRulesHighlighter::keyword_matchesWordInList() {
    RulesHighlighter hl;
    const int attrKw = hl.addAttribute({QColor("purple")});
    int klId = hl.addKeywordList({"ctrl", {QStringLiteral("if"), QStringLiteral("else")}, true});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::Keyword, {}, {}, {}, true, {}, klId, -1,
         attrKw, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("if foo else"), spans);
    // 'if' and 'else' should be highlighted. 'foo' is not in list.
    int kwCount = 0;
    for (const auto& s : spans) if (s.attributeId == attrKw) ++kwCount;
    QCOMPARE(kwCount, 2);
}

void TestRulesHighlighter::keyword_doesNotMatchPartialWord() {
    RulesHighlighter hl;
    const int attrKw = hl.addAttribute({QColor("purple")});
    int klId = hl.addKeywordList({"ctrl", {QStringLiteral("if")}, true});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::Keyword, {}, {}, {}, true, {}, klId, -1,
         attrKw, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("iffy"), spans); // should NOT match "if" at start
    for (const auto& s : spans) {
        QVERIFY(s.attributeId != attrKw);
    }
}

void TestRulesHighlighter::detectIdentifier_matchesAlnum() {
    RulesHighlighter hl;
    const int attrId = hl.addAttribute({QColor("brown")});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::DetectIdentifier, {}, {}, {}, true, {}, -1, -1,
         attrId, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("foo_bar 123"), spans);
    // "foo_bar" is an identifier (7 chars).
    bool ok = false;
    for (const auto& s : spans) {
        if (s.attributeId == attrId && s.start == 0 && s.length == 7) ok = true;
    }
    QVERIFY(ok);
}

void TestRulesHighlighter::detectSpaces_consumesWhitespaceRun() {
    RulesHighlighter hl;
    const int attrWs = hl.addAttribute({QColor("gray")});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::DetectSpaces, {}, {}, {}, true, {}, -1, -1,
         attrWs, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("   x"), spans);
    QCOMPARE(spans.first().attributeId, attrWs);
    QCOMPARE(spans.first().length, 3);
}

void TestRulesHighlighter::anyChar_singleCharInSet() {
    RulesHighlighter hl;
    const int attrOp = hl.addAttribute({QColor("orange")});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    hl.contextRef(ctxId).rules.push_back(
        {HighlightRule::AnyChar, {}, {}, QStringLiteral("+-*/"), true, {}, -1, -1,
         attrOp, -1, 0, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("a+b*c"), spans);
    int opCount = 0;
    for (const auto& s : spans) if (s.attributeId == attrOp) opCount += s.length;
    QCOMPARE(opCount, 2); // '+' and '*'
}

void TestRulesHighlighter::regExpr_numberLiteral() {
    RulesHighlighter hl;
    const int attrNum = hl.addAttribute({QColor("magenta")});
    int ctxId = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    HighlightRule r;
    r.kind = HighlightRule::RegExpr;
    r.regex = QRegularExpression(QStringLiteral("[0-9]+"));
    r.attributeId = attrNum;
    hl.contextRef(ctxId).rules.push_back(r);

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("x 123 y 45"), spans);
    int numCount = 0;
    for (const auto& s : spans) if (s.attributeId == attrNum) ++numCount;
    QCOMPARE(numCount, 2); // two number runs
}

void TestRulesHighlighter::context_stringSpansAreMerged() {
    RulesHighlighter hl;
    const int attrStr = hl.addAttribute({QColor("red")});
    int ctxNormal = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    int ctxString = hl.addContext({"String", attrStr, -1, 0, false, -1, {}});
    hl.contextRef(ctxNormal).rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrStr, ctxString, 0, false, false});
    hl.contextRef(ctxString).rules.push_back(
        {HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
         attrStr, -1, 1, false, false});

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("\"hello\""), spans);
    // All 7 chars share attrStr → one merged span.
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].length, 7);
}

void TestRulesHighlighter::context_multiLineComment_statePreservesAcrossLines() {
    RulesHighlighter hl;
    const int attrCom = hl.addAttribute({QColor("olive")});
    int ctxNormal = hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    int ctxComment = hl.addContext({"Comment", attrCom, -1, 0, false, -1, {}});
    hl.contextRef(ctxNormal).rules.push_back(
        {HighlightRule::Detect2Chars, '/', '*', {}, true, {}, -1, -1,
         attrCom, ctxComment, 0, false, false});
    hl.contextRef(ctxComment).rules.push_back(
        {HighlightRule::Detect2Chars, '*', '/', {}, true, {}, -1, -1,
         attrCom, -1, 1, false, false});

    QVector<StyleSpan> spans;
    HighlightState s0 = hl.initialState(), s1, s2;
    hl.highlightLine(QStringLiteral("x /* begin"), s0, spans, s1);
    // After first line: state should have Comment on stack (not just Normal).
    QVERIFY(s1 != s0);
    QCOMPARE(s1.contextStack.last(), ctxComment);

    spans.clear();
    hl.highlightLine(QStringLiteral("still comment */ end"), s1, spans, s2);
    // After second line: comment closed → back to Normal.
    QCOMPARE(s2.contextStack.last(), ctxNormal);
}

void TestRulesHighlighter::context_popReturnsToPreviousContext() {
    RulesHighlighter hl;
    const int attrA = hl.addAttribute({QColor("red")});
    int ctxA = hl.addContext({"A", -1, -1, 0, false, -1, {}});
    int ctxB = hl.addContext({"B", attrA, -1, 0, false, -1, {}});
    hl.contextRef(ctxA).rules.push_back(
        {HighlightRule::DetectChar, '(', {}, {}, true, {}, -1, -1,
         -1, ctxB, 0, false, false});
    hl.contextRef(ctxB).rules.push_back(
        {HighlightRule::DetectChar, ')', {}, {}, true, {}, -1, -1,
         -1, -1, 1, false, false});

    HighlightState sIn = hl.initialState(), sOut;
    QVector<StyleSpan> spans;
    hl.highlightLine(QStringLiteral("(x)"), sIn, spans, sOut);
    QCOMPARE(sOut.contextStack.last(), ctxA);
}

void TestRulesHighlighter::nothingMatches_emitsDefaultAttrPerChar() {
    RulesHighlighter hl;
    const int attrDefault = hl.addAttribute({QColor("black")});
    int ctxId = hl.addContext({"Normal", attrDefault, -1, 0, false, -1, {}});
    Q_UNUSED(ctxId);

    QVector<StyleSpan> spans;
    runLine(hl, QStringLiteral("abc"), spans);
    // Single merged span covering all 3 chars.
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].length, 3);
    QCOMPARE(spans[0].attributeId, attrDefault);
}

void TestRulesHighlighter::initialState_hasOneContextId() {
    RulesHighlighter hl;
    hl.addContext({"Normal", -1, -1, 0, false, -1, {}});
    HighlightState s = hl.initialState();
    QCOMPARE(s.contextStack.size(), 1);
    QCOMPARE(s.contextStack.first(), 0);
}

QTEST_APPLESS_MAIN(TestRulesHighlighter)
#include "test_rules_highlighter.moc"
