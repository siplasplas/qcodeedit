#include <qce/ViewportState.h>

#include <QtTest/QtTest>

class TestViewportState : public QObject {
    Q_OBJECT
private slots:
    void defaultState_isInvalid_andEmpty();
    void visibleLineCount_handlesEmpty();
    void visibleLineCount_handlesSingleLine();
    void visibleLineCount_handlesRange();
    void isValid_requiresAllDimensions();
};

void TestViewportState::defaultState_isInvalid_andEmpty() {
    qce::ViewportState s;
    QVERIFY(!s.isValid());
    QCOMPARE(s.visibleLineCount(), 0);
}

void TestViewportState::visibleLineCount_handlesEmpty() {
    qce::ViewportState s;
    s.firstVisibleLine = 0;
    s.lastVisibleLine = -1; // empty-document convention
    QCOMPARE(s.visibleLineCount(), 0);
}

void TestViewportState::visibleLineCount_handlesSingleLine() {
    qce::ViewportState s;
    s.firstVisibleLine = 5;
    s.lastVisibleLine = 5;
    QCOMPARE(s.visibleLineCount(), 1);
}

void TestViewportState::visibleLineCount_handlesRange() {
    qce::ViewportState s;
    s.firstVisibleLine = 10;
    s.lastVisibleLine = 29;
    QCOMPARE(s.visibleLineCount(), 20);
}

void TestViewportState::isValid_requiresAllDimensions() {
    qce::ViewportState s;
    s.lineHeight = 14;
    s.viewportWidth = 100;
    QVERIFY(!s.isValid()); // missing viewportHeight

    s.viewportHeight = 200;
    QVERIFY(s.isValid());

    s.lineHeight = 0;
    QVERIFY(!s.isValid());
}

QTEST_APPLESS_MAIN(TestViewportState)
#include "test_viewport_state.moc"
