#include <qce/FoldState.h>

#include <QtTest/QtTest>

using qce::FoldRegion;
using qce::FoldState;

class TestFoldState : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault();
    void singleLineRegions_areDropped();
    void setRegions_sortsByStartLine();
    void placeholderDefaultsToEllipsis();
    void isLineVisible_outsideRegionIsVisible();
    void isLineVisible_startLineAlwaysVisible();
    void isLineVisible_hiddenInsideCollapsed();
    void nestedRegions_depthComputed();
    void nestedRegions_outerCollapsedHidesAll();
    void regionStartingAt_returnsIndexOrMinusOne();
    void toggle_flipsCollapsedState();
    void foldAll_collapsesEveryRegion();
    void foldToLevel_onlyOutermost();
    void collapsedByDefault_appliedOnSetRegions();
    void depth_edgeCases();
    void depth_matchesBruteForce_randomRegions();
    void depth_largeInput();
    void setRegions_keepsCollapsedStateOfMatchingRegion();
    void setRegions_expandedDefaultRegionStaysExpanded();
    void shiftLines_insertedAndRemovedLines();
    void expandContaining_expandsOnlyHidingRegions();
    void clear_forgetsState();
};

// Reference definition of depth: regions that strictly contain `b`.
static int bruteForceDepth(const QVector<FoldRegion>& all, const FoldRegion& b) {
    int depth = 0;
    for (const FoldRegion& a : all) {
        const bool identical = a.startLine == b.startLine && a.startColumn == b.startColumn
            && a.endLine == b.endLine && a.endColumn == b.endColumn;
        if (identical) continue;
        const bool startsAtOrBefore = a.startLine < b.startLine
            || (a.startLine == b.startLine && a.startColumn <= b.startColumn);
        const bool endsAtOrAfter = a.endLine > b.endLine
            || (a.endLine == b.endLine && a.endColumn >= b.endColumn);
        if (startsAtOrBefore && endsAtOrAfter) ++depth;
    }
    return depth;
}

static FoldRegion makeRegion(int sl, int sc, int el, int ec,
                               const QString& group = QString(),
                               bool def = false) {
    FoldRegion r;
    r.startLine = sl; r.startColumn = sc;
    r.endLine = el;   r.endColumn = ec;
    r.group = group;
    r.collapsedByDefault = def;
    return r;
}

void TestFoldState::emptyByDefault() {
    FoldState s;
    QCOMPARE(s.regions().size(), 0);
    QVERIFY(s.isLineVisible(0));
    QCOMPARE(s.regionStartingAt(0), -1);
}

void TestFoldState::singleLineRegions_areDropped() {
    FoldState s;
    s.setRegions({makeRegion(3, 0, 3, 10), makeRegion(5, 0, 7, 0)});
    QCOMPARE(s.regions().size(), 1);
    QCOMPARE(s.regions().first().startLine, 5);
}

void TestFoldState::setRegions_sortsByStartLine() {
    FoldState s;
    s.setRegions({makeRegion(10, 0, 15, 0), makeRegion(2, 0, 8, 0)});
    QCOMPARE(s.regions().size(), 2);
    QCOMPARE(s.regions()[0].startLine, 2);
    QCOMPARE(s.regions()[1].startLine, 10);
}

void TestFoldState::placeholderDefaultsToEllipsis() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 2, 0)});
    QCOMPARE(s.regions().first().placeholder, QStringLiteral("\u2026"));
}

void TestFoldState::isLineVisible_outsideRegionIsVisible() {
    FoldState s;
    s.setRegions({makeRegion(3, 0, 5, 0)});
    s.setCollapsed(0, true);
    QVERIFY(s.isLineVisible(0));
    QVERIFY(s.isLineVisible(2));
    QVERIFY(s.isLineVisible(6));
}

void TestFoldState::isLineVisible_startLineAlwaysVisible() {
    FoldState s;
    s.setRegions({makeRegion(3, 0, 5, 0)});
    s.setCollapsed(0, true);
    QVERIFY(s.isLineVisible(3)); // startLine stays visible for the placeholder
}

void TestFoldState::isLineVisible_hiddenInsideCollapsed() {
    FoldState s;
    s.setRegions({makeRegion(3, 0, 5, 0)});
    s.setCollapsed(0, true);
    QVERIFY(!s.isLineVisible(4));
    QVERIFY(!s.isLineVisible(5));
}

void TestFoldState::nestedRegions_depthComputed() {
    FoldState s;
    s.setRegions({
        makeRegion(0, 0, 20, 0),   // outermost  → depth 0
        makeRegion(2, 0, 18, 0),   // middle     → depth 1
        makeRegion(5, 0, 10, 0),   // innermost  → depth 2
    });
    QCOMPARE(s.regions()[0].depth, 0);
    QCOMPARE(s.regions()[1].depth, 1);
    QCOMPARE(s.regions()[2].depth, 2);
}

void TestFoldState::nestedRegions_outerCollapsedHidesAll() {
    FoldState s;
    s.setRegions({
        makeRegion(0, 0, 10, 0),
        makeRegion(3, 0, 6, 0),
    });
    s.setCollapsed(0, true); // outer
    QVERIFY(!s.isLineVisible(3)); // inner-start line is also hidden
    QVERIFY(!s.isLineVisible(5));
    QVERIFY(!s.isLineVisible(10));
    QVERIFY(s.isLineVisible(0)); // outer start is visible
    QVERIFY(s.isLineVisible(11));
}

void TestFoldState::regionStartingAt_returnsIndexOrMinusOne() {
    FoldState s;
    s.setRegions({makeRegion(2, 0, 5, 0), makeRegion(10, 0, 15, 0)});
    QCOMPARE(s.regionStartingAt(2), 0);
    QCOMPARE(s.regionStartingAt(10), 1);
    QCOMPARE(s.regionStartingAt(7), -1);
}

void TestFoldState::toggle_flipsCollapsedState() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 2, 0)});
    QVERIFY(!s.isCollapsed(0));
    s.toggle(0);
    QVERIFY(s.isCollapsed(0));
    s.toggle(0);
    QVERIFY(!s.isCollapsed(0));
}

void TestFoldState::foldAll_collapsesEveryRegion() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 2, 0), makeRegion(4, 0, 6, 0)});
    s.foldAll();
    QVERIFY(s.isCollapsed(0));
    QVERIFY(s.isCollapsed(1));
    s.unfoldAll();
    QVERIFY(!s.isCollapsed(0));
    QVERIFY(!s.isCollapsed(1));
}

void TestFoldState::foldToLevel_onlyOutermost() {
    FoldState s;
    s.setRegions({
        makeRegion(0, 0, 20, 0),   // depth 0
        makeRegion(2, 0, 18, 0),   // depth 1
        makeRegion(5, 0, 10, 0),   // depth 2
    });
    s.foldToLevel(0);
    QVERIFY(s.isCollapsed(0));
    QVERIFY(!s.isCollapsed(1));
    QVERIFY(!s.isCollapsed(2));
}

void TestFoldState::collapsedByDefault_appliedOnSetRegions() {
    FoldState s;
    s.setRegions({
        makeRegion(0, 0, 2, 0, QStringLiteral("Header"), /*def*/true),
        makeRegion(4, 0, 6, 0),
    });
    QVERIFY(s.isCollapsed(0));
    QVERIFY(!s.isCollapsed(1));
}

void TestFoldState::depth_edgeCases() {
    auto depthOf = [](const FoldState& s, int sl, int sc, int el, int ec) {
        for (const FoldRegion& r : s.regions())
            if (r.startLine == sl && r.startColumn == sc && r.endLine == el && r.endColumn == ec)
                return r.depth;
        return -1;
    };

    FoldState s;
    s.setRegions({
        makeRegion(0, 0, 10, 0),
        makeRegion(0, 0, 10, 5),   // same start, same end line, larger end column
        makeRegion(0, 0, 4, 0),    // same start, earlier end
        makeRegion(2, 0, 10, 0),   // same end as the first
        makeRegion(5, 0, 15, 0),   // crosses (0,0)-(10,x): not contained
        makeRegion(2, 0, 10, 0),   // exact duplicate, dropped
    });
    QCOMPARE(s.regions().size(), 5);
    QCOMPARE(depthOf(s, 0, 0, 10, 5), 0);
    QCOMPARE(depthOf(s, 0, 0, 10, 0), 1);
    QCOMPARE(depthOf(s, 0, 0, 4, 0), 2);
    QCOMPARE(depthOf(s, 2, 0, 10, 0), 2);
    QCOMPARE(depthOf(s, 5, 0, 15, 0), 0);
    // Outer regions still come first for equal starts.
    QCOMPARE(s.regions()[0].endColumn, 5);
}

void TestFoldState::depth_matchesBruteForce_randomRegions() {
    QRandomGenerator rng(20261004);  // fixed seed: deterministic
    for (int round = 0; round < 200; ++round) {
        QVector<FoldRegion> input;
        const int n = int(rng.bounded(1, 40));
        for (int i = 0; i < n; ++i) {
            // Small ranges give many ties, shared starts/ends and crossings.
            const int sl = int(rng.bounded(0, 12));
            const int el = sl + int(rng.bounded(0, 8));
            input.push_back(makeRegion(sl, int(rng.bounded(0, 3)), el, int(rng.bounded(0, 3))));
        }
        FoldState s;
        s.setRegions(input);
        const auto& out = s.regions();
        for (const FoldRegion& r : out) {
            if (r.depth != bruteForceDepth(out, r)) {
                QFAIL(qPrintable(QStringLiteral("round %1: region %2:%3-%4:%5 depth %6, expected %7")
                                     .arg(round).arg(r.startLine).arg(r.startColumn)
                                     .arg(r.endLine).arg(r.endColumn).arg(r.depth)
                                     .arg(bruteForceDepth(out, r))));
            }
        }
    }
}

void TestFoldState::depth_largeInput() {
    // A deep chain plus many siblings, as in a large source file. The former
    // O(n^2) depth computation took seconds here.
    QVector<FoldRegion> input;
    const int chain = 20000;
    for (int i = 0; i < chain; ++i)
        input.push_back(makeRegion(i, 0, 2 * chain + 100000 - i, 0));
    for (int i = 0; i < 30000; ++i)
        input.push_back(makeRegion(chain + 3 * i, 0, chain + 3 * i + 1, 0));

    QElapsedTimer t;
    t.start();
    FoldState s;
    s.setRegions(input);
    qInfo() << "setRegions of" << input.size() << "regions:" << t.elapsed() << "ms";

    QCOMPARE(s.regions().size(), input.size());
    QCOMPARE(s.regions()[0].depth, 0);
    QCOMPARE(s.regions()[chain - 1].depth, chain - 1);
    QCOMPARE(s.regions()[chain].depth, chain);   // first sibling, inside the whole chain
    QCOMPARE(s.regions().last().depth, chain);
}

void TestFoldState::setRegions_keepsCollapsedStateOfMatchingRegion() {
    const QString curly = QStringLiteral("curly");
    FoldState s;
    s.setRegions({makeRegion(0, 0, 5, 0, curly), makeRegion(10, 4, 15, 0, curly)});
    s.setCollapsed(1, true);

    // Same start line and group, other columns/end (header or body edited).
    s.setRegions({makeRegion(0, 0, 5, 0, curly), makeRegion(10, 6, 16, 0, curly),
                  makeRegion(20, 0, 22, 0, curly)});
    QVERIFY(!s.isCollapsed(0));
    QVERIFY(s.isCollapsed(1));
    QVERIFY(!s.isCollapsed(2));

    // Another group on the same line is a different region.
    s.setRegions({makeRegion(10, 6, 16, 0, QStringLiteral("Comment"))});
    QVERIFY(!s.isCollapsed(0));

    // Two regions on one line are paired in column order.
    s.setRegions({makeRegion(3, 0, 9, 0, curly), makeRegion(3, 8, 6, 0, curly)});
    s.setCollapsed(1, true);
    s.setRegions({makeRegion(3, 1, 9, 0, curly), makeRegion(3, 9, 6, 0, curly)});
    QVERIFY(!s.isCollapsed(0));
    QVERIFY(s.isCollapsed(1));
}

void TestFoldState::setRegions_expandedDefaultRegionStaysExpanded() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 2, 0, QStringLiteral("Header"), /*def*/true)});
    QVERIFY(s.isCollapsed(0));
    s.setCollapsed(0, false);
    s.setRegions({makeRegion(0, 0, 3, 0, QStringLiteral("Header"), true),
                  makeRegion(5, 0, 7, 0, QStringLiteral("Header"), true)});
    QVERIFY(!s.isCollapsed(0));   // the user's choice wins
    QVERIFY(s.isCollapsed(1));    // a new region starts from its default
}

void TestFoldState::shiftLines_insertedAndRemovedLines() {
    FoldState s;
    s.setRegions({makeRegion(2, 0, 4, 0), makeRegion(10, 0, 14, 0), makeRegion(20, 0, 30, 0)});
    s.setCollapsed(1, true);

    s.shiftLines(5, 3);  // 3 lines inserted at line 5
    QCOMPARE(s.regions()[0].startLine, 2);
    QCOMPARE(s.regions()[0].endLine, 4);
    QCOMPARE(s.regions()[1].startLine, 13);
    QCOMPARE(s.regions()[1].endLine, 17);
    QVERIFY(s.isCollapsed(1));

    s.shiftLines(3, 1);  // inside region 0: its end moves, its start stays
    QCOMPARE(s.regions()[0].startLine, 2);
    QCOMPARE(s.regions()[0].endLine, 5);

    s.shiftLines(0, -2);  // lines 0..1 removed
    QCOMPARE(s.regions()[0].startLine, 0);
    QCOMPARE(s.regions()[1].startLine, 12);
    QVERIFY(s.isCollapsed(1));

    s.shiftLines(0, -1);  // the line region 0 starts on is removed: region dropped
    QCOMPARE(s.regions().size(), 2);
    QCOMPARE(s.regions()[0].startLine, 11);
    QVERIFY(s.isCollapsed(0));   // the collapsed region keeps its flag after reindexing
    QVERIFY(!s.isCollapsed(1));

    s.shiftLines(12, -10);  // removes the end of region 0 and the start of region 1
    QCOMPARE(s.regions().size(), 1);
    QCOMPARE(s.regions()[0].startLine, 11);
    QCOMPARE(s.regions()[0].endLine, 11);
}

void TestFoldState::expandContaining_expandsOnlyHidingRegions() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 10, 0), makeRegion(2, 0, 5, 0), makeRegion(20, 0, 25, 0)});
    s.setCollapsed(0, true);
    s.setCollapsed(1, true);
    s.setCollapsed(2, true);

    QVERIFY(!s.expandContaining(0));   // a start line is never hidden
    QVERIFY(s.expandContaining(3));    // hidden by regions 0 and 1
    QVERIFY(!s.isCollapsed(0));
    QVERIFY(!s.isCollapsed(1));
    QVERIFY(s.isCollapsed(2));
    QVERIFY(!s.expandContaining(3));
}

void TestFoldState::clear_forgetsState() {
    FoldState s;
    s.setRegions({makeRegion(0, 0, 4, 0)});
    s.setCollapsed(0, true);
    s.clear();
    QVERIFY(s.regions().isEmpty());
    s.setRegions({makeRegion(0, 0, 4, 0)});
    QVERIFY(!s.isCollapsed(0));
}

QTEST_APPLESS_MAIN(TestFoldState)
#include "test_fold_state.moc"
