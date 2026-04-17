#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/SimpleTextDocument.h>

#include <QtTest/QtTest>

using namespace qce;

/// Build a tiny highlighter with { / } and /* / */ that carry fold markers.
static std::unique_ptr<RulesHighlighter> makeBraceCommentHl() {
    auto hl = std::make_unique<RulesHighlighter>();
    const int attrSym = hl->addAttribute({QColor("black")});
    const int attrCom = hl->addAttribute({QColor("gray")});

    const int idCurly   = hl->regionIdForName(QStringLiteral("curly"));
    const int idComment = hl->regionIdForName(QStringLiteral("Comment"));

    const int cN = hl->addContext({"Normal", -1, -1, 0, false, -1, {}});
    const int cC = hl->addContext({"Comment", attrCom, -1, 0, false, -1, {}});

    HighlightRule openBrace;
    openBrace.kind = HighlightRule::DetectChar;
    openBrace.ch   = QLatin1Char('{');
    openBrace.attributeId   = attrSym;
    openBrace.beginRegionId = idCurly;
    hl->contextRef(cN).rules.push_back(openBrace);

    HighlightRule closeBrace;
    closeBrace.kind = HighlightRule::DetectChar;
    closeBrace.ch   = QLatin1Char('}');
    closeBrace.attributeId = attrSym;
    closeBrace.endRegionId = idCurly;
    hl->contextRef(cN).rules.push_back(closeBrace);

    HighlightRule openComment;
    openComment.kind = HighlightRule::Detect2Chars;
    openComment.ch   = QLatin1Char('/');
    openComment.ch1  = QLatin1Char('*');
    openComment.attributeId   = attrCom;
    openComment.nextContextId = cC;
    openComment.beginRegionId = idComment;
    hl->contextRef(cN).rules.push_back(openComment);

    HighlightRule closeComment;
    closeComment.kind = HighlightRule::Detect2Chars;
    closeComment.ch   = QLatin1Char('*');
    closeComment.ch1  = QLatin1Char('/');
    closeComment.attributeId = attrCom;
    closeComment.popCount    = 1;
    closeComment.endRegionId = idComment;
    hl->contextRef(cC).rules.push_back(closeComment);

    hl->setInitialContextId(cN);
    return hl;
}

class TestRuleBasedFolding : public QObject {
    Q_OBJECT
private slots:
    void singleBracePair_oneRegion();
    void nestedBraces_twoRegions();
    void multiLineComment_oneRegion();
    void unmatchedClose_isIgnored();
    void unmatchedOpen_atEnd_isIgnored();
    void placeholderPerGroup();
};

void TestRuleBasedFolding::singleBracePair_oneRegion() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("int f() {\n    return 1;\n}"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    const auto r = p.computeRegions(&doc);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].startLine, 0);
    QCOMPARE(r[0].startColumn, 8);  // column of '{'
    QCOMPARE(r[0].endLine, 2);
    QCOMPARE(r[0].endColumn, 1);    // one past '}' at col 0
    QCOMPARE(r[0].group, QStringLiteral("curly"));
}

void TestRuleBasedFolding::nestedBraces_twoRegions() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("{\n  {\n    x\n  }\n}"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    const auto r = p.computeRegions(&doc);
    QCOMPARE(r.size(), 2);
    // Inner closed first (its closing } is encountered before the outer)
    QCOMPARE(r[0].startLine, 1);
    QCOMPARE(r[0].endLine, 3);
    // Outer
    QCOMPARE(r[1].startLine, 0);
    QCOMPARE(r[1].endLine, 4);
}

void TestRuleBasedFolding::multiLineComment_oneRegion() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("x /* open\nstill\nclose */ y"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    const auto r = p.computeRegions(&doc);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].startLine, 0);
    QCOMPARE(r[0].endLine, 2);
    QCOMPARE(r[0].group, QStringLiteral("Comment"));
}

void TestRuleBasedFolding::unmatchedClose_isIgnored() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("}\n{\n}\n"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    const auto r = p.computeRegions(&doc);
    // First '}' is unmatched (no prior '{') → skipped. Remaining pair → 1 region.
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].startLine, 1);
    QCOMPARE(r[0].endLine, 2);
}

void TestRuleBasedFolding::unmatchedOpen_atEnd_isIgnored() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("{\n  no close here\n"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    const auto r = p.computeRegions(&doc);
    QCOMPARE(r.size(), 0);
}

void TestRuleBasedFolding::placeholderPerGroup() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("x /* a\nb */ y"));
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    p.setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
    const auto r = p.computeRegions(&doc);
    QCOMPARE(r.size(), 1);
    QCOMPARE(r[0].placeholder, QStringLiteral("/*…*/"));
}

QTEST_APPLESS_MAIN(TestRuleBasedFolding)
#include "test_rule_based_folding.moc"
