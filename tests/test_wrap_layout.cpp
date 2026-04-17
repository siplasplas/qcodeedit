#include "WrapLayout.h"

#include <qce/SimpleTextDocument.h>

#include <QtTest/QtTest>

class TestWrapLayout : public QObject {
    Q_OBJECT
private slots:
    void shortLine_singleRow();
    void longLine_wrapsAtWordBoundary();
    void longLine_hardBreakWhenNoSpace();
    void multiLine_correctRowCount();
    void emptyLine_singleRow();
    void rowForCursor_wrappedLine();
    void firstRowOf_correct();
};

using qce::WrapLayout;
using qce::SimpleTextDocument;

// Helper: build layout with tabWidth=1 so visual cols = char count.
static WrapLayout buildLayout(SimpleTextDocument& doc, int maxCols) {
    WrapLayout wl;
    wl.rebuild(&doc, maxCols, 1);
    return wl;
}

void TestWrapLayout::shortLine_singleRow() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello"));
    WrapLayout wl = buildLayout(doc, 20);
    QCOMPARE(wl.totalRows(), 1);
    QCOMPARE(wl.rowAt(0).startCol, 0);
    QCOMPARE(wl.rowAt(0).endCol, 5);
}

void TestWrapLayout::longLine_wrapsAtWordBoundary() {
    SimpleTextDocument doc;
    // "hello world" — maxCols=8: "hello " fits (6 chars incl. space), "world" next row
    doc.setText(QStringLiteral("hello world"));
    WrapLayout wl = buildLayout(doc, 8);
    QCOMPARE(wl.totalRows(), 2);
    QCOMPARE(wl.rowAt(0).startCol, 0);
    QCOMPARE(wl.rowAt(0).endCol, 6); // "hello " (break after space)
    QCOMPARE(wl.rowAt(1).startCol, 6);
    QCOMPARE(wl.rowAt(1).endCol, 11);
}

void TestWrapLayout::longLine_hardBreakWhenNoSpace() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("abcdefgh")); // 8 chars, maxCols=5 → hard break at 5
    WrapLayout wl = buildLayout(doc, 5);
    QCOMPARE(wl.totalRows(), 2);
    QCOMPARE(wl.rowAt(0).endCol, 5);
    QCOMPARE(wl.rowAt(1).startCol, 5);
}

void TestWrapLayout::multiLine_correctRowCount() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("short\na longer line here\nend"));
    // maxCols=10: "short"→1 row, "a longer l"/"ine here"→2 rows (break after space at 9),
    //             "end"→1 row
    WrapLayout wl = buildLayout(doc, 10);
    // line 0 (5 chars): 1 row
    QCOMPARE(wl.rowCountOf(0), 1);
    // line 1 (20 chars): must be > 1 row
    QVERIFY(wl.rowCountOf(1) >= 2);
    // line 2 (3 chars): 1 row
    QCOMPARE(wl.rowCountOf(2), 1);
    // All logical lines covered
    QCOMPARE(wl.rowAt(wl.firstRowOf(0)).logicalLine, 0);
    QCOMPARE(wl.rowAt(wl.firstRowOf(2)).logicalLine, 2);
}

void TestWrapLayout::emptyLine_singleRow() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("\n\n")); // 2 empty lines (trailing \n stripped → 2 lines)
    WrapLayout wl = buildLayout(doc, 10);
    QCOMPARE(wl.totalRows(), 2);
    QCOMPARE(wl.rowAt(0).startCol, 0);
    QCOMPARE(wl.rowAt(0).endCol, 0);
}

void TestWrapLayout::rowForCursor_wrappedLine() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello world")); // wraps at col 6 with maxCols=8
    WrapLayout wl = buildLayout(doc, 8);
    QCOMPARE(wl.rowForCursor(0, 0), 0); // 'h' → first row
    QCOMPARE(wl.rowForCursor(0, 5), 0); // 'o' → first row
    QCOMPARE(wl.rowForCursor(0, 6), 1); // 'w' → second row
    QCOMPARE(wl.rowForCursor(0, 10), 1); // 'd' → second row
}

void TestWrapLayout::firstRowOf_correct() {
    SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello world\nshort"));
    WrapLayout wl = buildLayout(doc, 8);
    QCOMPARE(wl.firstRowOf(0), 0);
    // line 0 has 2 rows, so line 1 starts at row 2
    QCOMPARE(wl.firstRowOf(1), 2);
    QCOMPARE(wl.rowAt(wl.firstRowOf(1)).logicalLine, 1);
}

QTEST_APPLESS_MAIN(TestWrapLayout)
#include "test_wrap_layout.moc"
