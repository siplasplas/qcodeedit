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
};

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

QTEST_APPLESS_MAIN(TestFoldState)
#include "test_fold_state.moc"
