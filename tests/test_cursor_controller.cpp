#include "CursorController.h"

#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>

#include <QtTest/QtTest>

class TestCursorController : public QObject {
    Q_OBJECT
private slots:
    // Setup
    void init();

    // No document
    void noDocument_everyMoveIsNoOp();

    // Clamp
    void clamp_emptyDocument_returnsOrigin();
    void clamp_lineOutOfRange_snapsToLastLine();
    void clamp_columnBeyondLine_snapsToLineEnd();

    // Vertical
    void moveUp_atTopLine_staysAtTop();
    void moveUp_preservesColumnWhenPossible();
    void moveUp_clampsColumnToShorterLine();
    void moveDown_atLastLine_staysAtLast();
    void moveDown_stepsByMultipleLines();

    // Horizontal
    void moveLeft_atColumnZero_wrapsToPreviousLineEnd();
    void moveLeft_atOriginIsNoOp();
    void moveRight_atLineEnd_wrapsToNextLineStart();
    void moveRight_atDocumentEndIsNoOp();

    // Absolute
    void moveToLineStart_setsColumnZero();
    void moveToLineEnd_setsColumnToLineLength();
    void moveToDocumentStart_returnsOrigin();
    void moveToDocumentEnd_returnsLastPosition();

    // Page
    void movePageDown_stepsByPageLines();
    void movePageDown_pageLinesZero_clampsToOne();

    // Word movement
    void moveWordRight_skipsWordThenSpace();
    void moveWordRight_atLineEnd_wrapsToNextLine();
    void moveWordLeft_skipsSpaceThenWord();
    void moveWordLeft_atColumnZero_wrapsToPreviousLine();

private:
    qce::SimpleTextDocument m_doc;
};

using qce::CursorController;
using qce::TextCursor;

// --- Setup --------------------------------------------------------------

void TestCursorController::init() {
    // Fresh document for each test: 4 lines of varying length.
    m_doc.setLines({
        QStringLiteral("short"),        // 0: len 5
        QStringLiteral("a much longer line"), // 1: len 18
        QStringLiteral(""),             // 2: len 0
        QStringLiteral("mid length"),   // 3: len 10
    });
}

// --- No document --------------------------------------------------------

void TestCursorController::noDocument_everyMoveIsNoOp() {
    CursorController cc(nullptr);
    const TextCursor start{0, 0};
    QCOMPARE(cc.moveUp(start), start);
    QCOMPARE(cc.moveDown(start), start);
    QCOMPARE(cc.moveLeft(start), start);
    QCOMPARE(cc.moveRight(start), start);
    QCOMPARE(cc.moveToDocumentEnd(start), start);
}

// --- Clamp --------------------------------------------------------------

void TestCursorController::clamp_emptyDocument_returnsOrigin() {
    qce::SimpleTextDocument empty;
    CursorController cc(&empty);
    QCOMPARE(cc.clamp(TextCursor{5, 3}), (TextCursor{0, 0}));
}

void TestCursorController::clamp_lineOutOfRange_snapsToLastLine() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.clamp(TextCursor{99, 0});
    QCOMPARE(r.line, 3); // last line
}

void TestCursorController::clamp_columnBeyondLine_snapsToLineEnd() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.clamp(TextCursor{0, 100});
    QCOMPARE(r, (TextCursor{0, 5})); // line 0 has length 5
}

// --- Vertical -----------------------------------------------------------

void TestCursorController::moveUp_atTopLine_staysAtTop() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveUp(TextCursor{0, 2});
    QCOMPARE(r, (TextCursor{0, 2}));
}

void TestCursorController::moveUp_preservesColumnWhenPossible() {
    CursorController cc(&m_doc);
    // From line 1 col 10 (within both lines) up to line 0, col 5 (clamped).
    // Check that moving from a column that fits stays.
    const TextCursor r = cc.moveUp(TextCursor{3, 5});
    QCOMPARE(r.line, 2);
    QCOMPARE(r.column, 0); // line 2 is empty
}

void TestCursorController::moveUp_clampsColumnToShorterLine() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveUp(TextCursor{1, 18});
    QCOMPARE(r.line, 0);
    QCOMPARE(r.column, 5); // clamped to line 0 length
}

void TestCursorController::moveDown_atLastLine_staysAtLast() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveDown(TextCursor{3, 4});
    QCOMPARE(r, (TextCursor{3, 4}));
}

void TestCursorController::moveDown_stepsByMultipleLines() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveDown(TextCursor{0, 3}, 2);
    QCOMPARE(r.line, 2);
    QCOMPARE(r.column, 0); // line 2 is empty
}

// --- Horizontal ---------------------------------------------------------

void TestCursorController::moveLeft_atColumnZero_wrapsToPreviousLineEnd() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveLeft(TextCursor{1, 0});
    QCOMPARE(r, (TextCursor{0, 5})); // end of line 0
}

void TestCursorController::moveLeft_atOriginIsNoOp() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveLeft(TextCursor{0, 0});
    QCOMPARE(r, (TextCursor{0, 0}));
}

void TestCursorController::moveRight_atLineEnd_wrapsToNextLineStart() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveRight(TextCursor{0, 5}); // end of line 0
    QCOMPARE(r, (TextCursor{1, 0}));
}

void TestCursorController::moveRight_atDocumentEndIsNoOp() {
    CursorController cc(&m_doc);
    const TextCursor end{3, 10}; // end of last line
    QCOMPARE(cc.moveRight(end), end);
}

// --- Absolute -----------------------------------------------------------

void TestCursorController::moveToLineStart_setsColumnZero() {
    CursorController cc(&m_doc);
    QCOMPARE(cc.moveToLineStart(TextCursor{2, 7}), (TextCursor{2, 0}));
}

void TestCursorController::moveToLineEnd_setsColumnToLineLength() {
    CursorController cc(&m_doc);
    QCOMPARE(cc.moveToLineEnd(TextCursor{1, 0}), (TextCursor{1, 18}));
}

void TestCursorController::moveToDocumentStart_returnsOrigin() {
    CursorController cc(&m_doc);
    QCOMPARE(cc.moveToDocumentStart(TextCursor{3, 5}), (TextCursor{0, 0}));
}

void TestCursorController::moveToDocumentEnd_returnsLastPosition() {
    CursorController cc(&m_doc);
    QCOMPARE(cc.moveToDocumentEnd(TextCursor{0, 0}), (TextCursor{3, 10}));
}

// --- Page ---------------------------------------------------------------

void TestCursorController::movePageDown_stepsByPageLines() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.movePageDown(TextCursor{0, 0}, 2);
    QCOMPARE(r.line, 2);
}

void TestCursorController::movePageDown_pageLinesZero_clampsToOne() {
    CursorController cc(&m_doc);
    const TextCursor r = cc.movePageDown(TextCursor{0, 0}, 0);
    QCOMPARE(r.line, 1); // clamped to 1 line move
}

// --- Word movement -------------------------------------------------------

void TestCursorController::moveWordRight_skipsWordThenSpace() {
    // Line 1: "a much longer line"
    // From col 0 ('a') → skip 'a', skip ' ' → col 2 (start of 'much')
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveWordRight(TextCursor{1, 0});
    QCOMPARE(r.line, 1);
    QCOMPARE(r.column, 2);
}

void TestCursorController::moveWordRight_atLineEnd_wrapsToNextLine() {
    CursorController cc(&m_doc);
    // Line 0 ends at col 5; move right wraps to line 1 col 0.
    const TextCursor r = cc.moveWordRight(TextCursor{0, 5});
    QCOMPARE(r.line, 1);
    QCOMPARE(r.column, 0);
}

void TestCursorController::moveWordLeft_skipsSpaceThenWord() {
    // Line 1: "a much longer line", col 7 (middle of 'longer')
    // Skip non-ws 'onger' → col 2; skip ' ' → col 1 (end of 'a'); skip 'a' → col 0
    // Actually: skip non-ws to the left from col 7: 'l','o','n','g' back to col 2 start of 'longer'
    // Then skip ws: ' ' → col 1. Then skip non-ws: 'a' → col 0.
    // But wait: the implementation skips whitespace first, then non-whitespace.
    // From col 7 ('o' in 'longer'): line[6]='l', not space, skip ws: nothing.
    // Skip non-ws to the left: 'l','o','n','g','e','r'... back to col 2 (start of 'longer')?
    // Wait let me re-check: line = "a much longer line"
    // indices: 0='a', 1=' ', 2='m', 3='u', 4='c', 5='h', 6=' ', 7='l', 8='o', ...
    // From col 7 ('l'): skip ws left (line[6]=' ') → col 6; skip non-ws left: line[5]='h', 4='c',3='u',2='m' → col 2
    CursorController cc(&m_doc);
    const TextCursor r = cc.moveWordLeft(TextCursor{1, 7});
    QCOMPARE(r.line, 1);
    QCOMPARE(r.column, 2);
}

void TestCursorController::moveWordLeft_atColumnZero_wrapsToPreviousLine() {
    CursorController cc(&m_doc);
    // Line 1, col 0 → wraps to end of line 0 (col 5).
    const TextCursor r = cc.moveWordLeft(TextCursor{1, 0});
    QCOMPARE(r.line, 0);
    QCOMPARE(r.column, 5);
}

QTEST_APPLESS_MAIN(TestCursorController)
#include "test_cursor_controller.moc"
