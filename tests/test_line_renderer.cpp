#include "LineRenderer.h"

#include <QtTest/QtTest>

class TestLineRenderer : public QObject {
    Q_OBJECT
private slots:
    void visualColumn_noTabs();
    void visualColumn_singleTab_atStart();
    void visualColumn_tabInMiddle();
    void visualColumn_multipleTabs();
    void visualColumn_pastEnd_returnsEndVisual();
};

using qce::LineRenderer;

void TestLineRenderer::visualColumn_noTabs() {
    // "abcde", tabWidth=4: visual == logical
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("abcde"), 3, 4), 3);
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("abcde"), 5, 4), 5);
}

void TestLineRenderer::visualColumn_singleTab_atStart() {
    // "\tabc", tabWidth=4: tab at col 0 → next stop = 4
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("\tabc"), 1, 4), 4);
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("\tabc"), 2, 4), 5);
}

void TestLineRenderer::visualColumn_tabInMiddle() {
    // "ab\tc", tabWidth=4: visual of 'a'=0,'b'=1, then tab 1→4 (advances 3), 'c'=4
    // visualColumn at charIndex 3 (after tab) = 4
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("ab\tc"), 3, 4), 4);
    // charIndex 2 (at tab start) = 2
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("ab\tc"), 2, 4), 2);
}

void TestLineRenderer::visualColumn_multipleTabs() {
    // "\t\t", tabWidth=4: first tab → 4, second tab → 8
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("\t\t"), 1, 4), 4);
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("\t\t"), 2, 4), 8);
}

void TestLineRenderer::visualColumn_pastEnd_returnsEndVisual() {
    // charIndex beyond line size is clamped to line.size()
    QCOMPARE(LineRenderer::visualColumn(QStringLiteral("abc"), 10, 4), 3);
}

QTEST_APPLESS_MAIN(TestLineRenderer)
#include "test_line_renderer.moc"
