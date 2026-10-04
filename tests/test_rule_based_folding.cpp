#include <qce/CodeEditArea.h>
#include <qce/FoldState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/SimpleTextDocument.h>

#include <QScrollBar>
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
    void regionsFromLineMarkers_matchesComputeRegions();
    void regionsFromLineMarkers_rejectsOtherHighlighter();
    void editor_usesMarkersAndMatchesFreshComputation();
    void smallDocument_isHighlightedAtOnce();
    void largeDocument_highlightsLazilyThenFolds();
    void largeDocument_paintHighlightsVisibleLines();
    void largeDocument_editsDuringBackgroundHighlighting();
};

// A document well above CodeEditArea::kSyncHighlightLines: blocks of
// "{ /* c */\n  body\n}\n", with a comment spanning lines every 50 blocks.
static QString largeText() {
    QString s;
    const int blocks = CodeEditArea::kSyncHighlightLines;   // 3 lines each
    for (int i = 0; i < blocks; ++i) {
        s += (i % 50 == 0) ? QStringLiteral("/* multi\n line */ {\n  body\n}\n")
                           : QStringLiteral("{ /* c */\n  body\n}\n");
    }
    return s;
}

static void compareRegions(const QVector<FoldRegion>& got, const QVector<FoldRegion>& expected) {
    QCOMPARE(got.size(), expected.size());
    for (int i = 0; i < got.size(); ++i) {
        QCOMPARE(got[i].startLine,   expected[i].startLine);
        QCOMPARE(got[i].startColumn, expected[i].startColumn);
        QCOMPARE(got[i].endLine,     expected[i].endLine);
        QCOMPARE(got[i].endColumn,   expected[i].endColumn);
        QCOMPARE(got[i].group,       expected[i].group);
        QCOMPARE(got[i].placeholder, expected[i].placeholder);
    }
}

static QVector<QVector<FoldMarker>> collectMarkers(const IHighlighter& hl,
                                                   const SimpleTextDocument& doc) {
    QVector<QVector<FoldMarker>> markers(doc.lineCount());
    HighlightState state = hl.initialState();
    QVector<StyleSpan> spans;
    for (int i = 0; i < doc.lineCount(); ++i) {
        HighlightState next;
        hl.highlightLineWithFolds(doc.lineAt(i), state, spans, next, markers[i]);
        state = next;
    }
    return markers;
}

/// Counts calls to the full-pass computeRegions().
class CountingProvider : public RuleBasedFoldingProvider {
public:
    using RuleBasedFoldingProvider::RuleBasedFoldingProvider;
    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override {
        ++calls;
        return RuleBasedFoldingProvider::computeRegions(doc);
    }
    mutable int calls = 0;
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

void TestRuleBasedFolding::regionsFromLineMarkers_matchesComputeRegions() {
    const QStringList texts{
        QStringLiteral("int f() {\n    return 1;\n}"),
        QStringLiteral("{\n  {\n    x\n  }\n}"),
        QStringLiteral("}\n{\n}\n{\n  no close"),
        QStringLiteral("a /* {\n } still comment\n*/ {\n  b\n}"),   // braces inside a comment
        QStringLiteral("{ /* x\n y */ }\n{\n{\n}\n}\n/* a\nb */"),
    };
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    p.setPlaceholderFor(QStringLiteral("Comment"), QStringLiteral("/*…*/"));
    for (const QString& text : texts) {
        SimpleTextDocument doc;
        doc.setText(text);
        QVector<FoldRegion> fromMarkers;
        QVERIFY(p.regionsFromLineMarkers(hl.get(), collectMarkers(*hl, doc), fromMarkers));
        compareRegions(fromMarkers, p.computeRegions(&doc));
    }
}

void TestRuleBasedFolding::regionsFromLineMarkers_rejectsOtherHighlighter() {
    auto hl = makeBraceCommentHl();
    auto other = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("{\n}"));
    QVector<FoldRegion> regions;
    QVERIFY(!p.regionsFromLineMarkers(other.get(), collectMarkers(*other, doc), regions));
}

void TestRuleBasedFolding::editor_usesMarkersAndMatchesFreshComputation() {
    auto hl = makeBraceCommentHl();
    CountingProvider p(hl.get());
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("a {\n  b\n}\nc /* x\n  y */\n{\n  {\n  }\n}\nend"));
    CodeEditArea area;
    area.setDocument(&doc);
    area.setHighlighter(hl.get());
    area.setFoldingProvider(&p);

    auto expectFresh = [&](const char* step) {
        FoldState fresh;
        fresh.setRegions(p.RuleBasedFoldingProvider::computeRegions(&doc));
        const auto& got = area.foldState().regions();
        if (got.size() != fresh.regions().size())
            QFAIL(qPrintable(QStringLiteral("%1: %2 regions, expected %3")
                                 .arg(QLatin1String(step)).arg(got.size())
                                 .arg(fresh.regions().size())));
        compareRegions(got, fresh.regions());
    };
    expectFresh("load");

    // Opening a comment changes the highlighting state of every later line.
    area.setCursorPosition({0, 0});
    QTest::keyClicks(&area, QStringLiteral("/*"));
    expectFresh("open comment");

    // Multi-line insert: the document reports the changed line first.
    area.setCursorPosition({2, 1});
    QTest::keyClick(&area, Qt::Key_Return);
    QTest::keyClicks(&area, QStringLiteral("*/ {"));
    QTest::keyClick(&area, Qt::Key_Return);
    QTest::keyClick(&area, Qt::Key_Return);
    QTest::keyClicks(&area, QStringLiteral("}"));
    expectFresh("insert lines");

    // One multi-line insert that also changes the state of all later lines
    // (an input-method commit goes through the same insert path as paste).
    area.setCursorPosition({1, 0});
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("x\n/* opened"));
    QCoreApplication::sendEvent(&area, &commit);
    expectFresh("multi-line insert opening a comment");
    QTest::keyClicks(&area, QStringLiteral(" */"));
    expectFresh("close that comment");

    // Multi-line removal through a selection.
    area.setSelection({1, 0}, {6, 1});
    QTest::keyClick(&area, Qt::Key_Backspace);
    expectFresh("remove selection");

    // Joining lines with Backspace at a line start.
    area.setCursorPosition({2, 0});
    QTest::keyClick(&area, Qt::Key_Backspace);
    expectFresh("join lines");

    while (area.canUndo()) area.undo();
    QCOMPARE(doc.toPlainText(),
             QStringLiteral("a {\n  b\n}\nc /* x\n  y */\n{\n  {\n  }\n}\nend"));
    expectFresh("undo all");

    while (area.canRedo()) area.redo();
    expectFresh("redo all");

    QCOMPARE(p.calls, 0);   // never fell back to the full second pass
}

void TestRuleBasedFolding::smallDocument_isHighlightedAtOnce() {
    auto hl = makeBraceCommentHl();
    RuleBasedFoldingProvider p(hl.get());
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("{\n  x\n}\n"));
    CodeEditArea area;
    area.setDocument(&doc);
    area.setHighlighter(hl.get());
    area.setFoldingProvider(&p);
    QCOMPARE(area.highlightedLineCount(), doc.lineCount());
    QCOMPARE(area.foldState().regions().size(), 1);
}

void TestRuleBasedFolding::largeDocument_highlightsLazilyThenFolds() {
    auto hl = makeBraceCommentHl();
    CountingProvider p(hl.get());
    SimpleTextDocument doc;
    doc.setText(largeText());
    QVERIFY(doc.lineCount() > CodeEditArea::kSyncHighlightLines);
    CodeEditArea area;
    area.setDocument(&doc);
    QSignalSpy completed(&area, &CodeEditArea::highlightingCompleted);
    area.setHighlighter(hl.get());
    area.setFoldingProvider(&p);

    // Nothing ran yet: no event loop since setHighlighter().
    QVERIFY(area.highlightedLineCount() < doc.lineCount());
    QVERIFY(area.foldState().regions().isEmpty());   // folds wait for the end

    QTRY_COMPARE_WITH_TIMEOUT(area.highlightedLineCount(), doc.lineCount(), 10000);
    QTRY_COMPARE(completed.size(), 1);
    FoldState fresh;
    fresh.setRegions(p.RuleBasedFoldingProvider::computeRegions(&doc));
    compareRegions(area.foldState().regions(), fresh.regions());
    QCOMPARE(p.calls, 0);
}

void TestRuleBasedFolding::largeDocument_paintHighlightsVisibleLines() {
    auto hl = makeBraceCommentHl();
    SimpleTextDocument doc;
    doc.setText(largeText());
    CodeEditArea area;
    area.resize(400, 300);
    area.setDocument(&doc);
    area.setHighlighter(hl.get());
    area.show();
    // grab() paints synchronously (and applies the pending resize) without
    // running the event loop, so the background timer has not run.
    area.verticalScrollBar()->setValue(area.verticalScrollBar()->maximum());
    area.viewport()->grab();
    const auto vp = area.viewportState();
    QVERIFY(vp.lastVisibleLine >= doc.lineCount() - 2);
    QCOMPARE(area.highlightedLineCount(), vp.lastVisibleLine + 1);
}

void TestRuleBasedFolding::largeDocument_editsDuringBackgroundHighlighting() {
    auto hl = makeBraceCommentHl();
    CountingProvider p(hl.get());
    SimpleTextDocument doc;
    doc.setText(largeText());
    CodeEditArea area;
    area.setDocument(&doc);
    area.setHighlighter(hl.get());
    area.setFoldingProvider(&p);

    // Inside the (empty) prefix region near the top, far below it, and a
    // multi-line insert that opens a comment, all before the timer ran.
    area.setCursorPosition({1, 0});
    QTest::keyClicks(&area, QStringLiteral("/* "));
    area.setCursorPosition({doc.lineCount() - 3, 0});
    QTest::keyClick(&area, Qt::Key_Return);
    QTest::keyClicks(&area, QStringLiteral("{ x"));
    QTest::keyClick(&area, Qt::Key_Return);
    QTest::keyClicks(&area, QStringLiteral("}"));
    area.setSelection({4, 0}, {9, 1});
    QTest::keyClick(&area, Qt::Key_Backspace);
    QVERIFY(area.highlightedLineCount() < doc.lineCount());

    // Wait part of the way, edit again in the highlighted prefix.
    QTRY_VERIFY(area.highlightedLineCount() > 100);
    area.setCursorPosition({2, 0});
    QTest::keyClicks(&area, QStringLiteral("*/"));

    QTRY_COMPARE_WITH_TIMEOUT(area.highlightedLineCount(), doc.lineCount(), 10000);
    QTRY_VERIFY(!area.foldState().regions().isEmpty());
    FoldState fresh;
    fresh.setRegions(p.RuleBasedFoldingProvider::computeRegions(&doc));
    compareRegions(area.foldState().regions(), fresh.regions());
    QCOMPARE(p.calls, 0);

    // After completion edits update folds immediately again.
    area.setCursorPosition({0, 0});
    QTest::keyClicks(&area, QStringLiteral("{"));
    QTest::keyClick(&area, Qt::Key_Return);
    fresh.setRegions(p.RuleBasedFoldingProvider::computeRegions(&doc));
    compareRegions(area.foldState().regions(), fresh.regions());
}

QTEST_MAIN(TestRuleBasedFolding)  // CodeEditArea needs a QApplication
#include "test_rule_based_folding.moc"
