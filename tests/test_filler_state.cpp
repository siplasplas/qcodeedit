#include <qce/FillerState.h>

#include <QtTest/QtTest>

using qce::FillerLine;
using qce::FillerState;

class TestFillerState : public QObject {
    Q_OBJECT
private slots:
    void emptyByDefault();
    void setFillers_dropsZeroRowCountAndNegativeBeforeLine();
    void setFillers_sortsByBeforeLine();
    void setFillers_mergesDuplicatesAtSameBeforeLine();
    void totalFillerRows_sumsRowCount();
    void fillerRowsBeforeOrAt_cumulativeSum();
};

void TestFillerState::emptyByDefault() {
    FillerState s;
    QCOMPARE(s.fillers().size(), 0);
    QCOMPARE(s.totalFillerRows(), 0);
    QCOMPARE(s.fillerRowsBeforeOrAt(0), 0);
    QCOMPARE(s.fillerRowsBeforeOrAt(999), 0);
}

void TestFillerState::setFillers_dropsZeroRowCountAndNegativeBeforeLine() {
    FillerState s;
    s.setFillers({
        {5, 0, QColor(), QString()},     // dropped: rowCount <= 0
        {-1, 2, QColor(), QString()},    // dropped: beforeLine < 0
        {5, 2, QColor("red"), QString()} // kept
    });
    QCOMPARE(s.fillers().size(), 1);
    QCOMPARE(s.fillers().first().beforeLine, 5);
    QCOMPARE(s.fillers().first().rowCount, 2);
}

void TestFillerState::setFillers_sortsByBeforeLine() {
    FillerState s;
    s.setFillers({
        {10, 1, QColor(), QString()},
        {3,  2, QColor(), QString()},
        {7,  1, QColor(), QString()},
    });
    QCOMPARE(s.fillers().size(), 3);
    QCOMPARE(s.fillers()[0].beforeLine, 3);
    QCOMPARE(s.fillers()[1].beforeLine, 7);
    QCOMPARE(s.fillers()[2].beforeLine, 10);
}

void TestFillerState::setFillers_mergesDuplicatesAtSameBeforeLine() {
    FillerState s;
    s.setFillers({
        {5, 2, QColor("red"),   QStringLiteral("a")},
        {5, 3, QColor("green"), QStringLiteral("b")},
    });
    QCOMPARE(s.fillers().size(), 1);
    QCOMPARE(s.fillers().first().rowCount, 5);
    QCOMPARE(s.fillers().first().fillColor, QColor("red"));
    QCOMPARE(s.fillers().first().label,     QStringLiteral("a"));
}

void TestFillerState::totalFillerRows_sumsRowCount() {
    FillerState s;
    s.setFillers({
        {1, 2, QColor(), QString()},
        {5, 3, QColor(), QString()},
    });
    QCOMPARE(s.totalFillerRows(), 5);
}

void TestFillerState::fillerRowsBeforeOrAt_cumulativeSum() {
    FillerState s;
    s.setFillers({
        {1, 2, QColor(), QString()},
        {5, 3, QColor(), QString()},
        {9, 1, QColor(), QString()},
    });
    QCOMPARE(s.fillerRowsBeforeOrAt(0), 0);
    QCOMPARE(s.fillerRowsBeforeOrAt(1), 2); // includes block at beforeLine=1
    QCOMPARE(s.fillerRowsBeforeOrAt(4), 2);
    QCOMPARE(s.fillerRowsBeforeOrAt(5), 5);
    QCOMPARE(s.fillerRowsBeforeOrAt(100), 6);
}

QTEST_APPLESS_MAIN(TestFillerState)
#include "test_filler_state.moc"
