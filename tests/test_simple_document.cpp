#include <qce/SimpleTextDocument.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

class TestSimpleDocument : public QObject {
    Q_OBJECT
private slots:
    void emptyDocument_hasZeroLines();
    void setLines_populatesLinesAndEmitsReset();
    void lineAt_outOfRange_returnsEmpty();
    void setText_splitsOnLF();
    void setText_normalizesCRLF();
    void setText_emptyString_givesZeroLines();
    void setText_singleNewline_givesOneEmptyLine();
    void setText_trailingNewline_doesNotCreateExtraEmpty();
    void toPlainText_roundTrip();
    void maxLineLength_emptyDocument_isZero();
    void maxLineLength_computesAndCaches();
    void maxLineLength_invalidatesAfterSetLines();

    // insertText
    void insertText_singleLine_inMiddle();
    void insertText_multiLine();
    void insertText_atEnd_appendsText();
    void insertText_returnsCorrectCursor();
    void insertText_emitsLinesChanged();
    void insertText_emitsLinesInserted_forMultiLine();

    // removeText
    void removeText_singleLine_inMiddle();
    void removeText_multiLine();
    void removeText_samePosition_returnsEmpty();
    void removeText_returnsRemovedText();
    void removeText_emitsLinesChanged();
    void stripTrailingWhitespace_removesSpacesAndTabs();
    void stripTrailingWhitespace_leavesCleanLinesUntouched();
    void removeText_emitsLinesRemoved_forMultiLine();
};

void TestSimpleDocument::emptyDocument_hasZeroLines() {
    qce::SimpleTextDocument doc;
    QCOMPARE(doc.lineCount(), 0);
    QCOMPARE(doc.maxLineLength(), 0);
    QCOMPARE(doc.toPlainText(), QString());
}

void TestSimpleDocument::setLines_populatesLinesAndEmitsReset() {
    qce::SimpleTextDocument doc;
    QSignalSpy spy(&doc, &qce::ITextDocument::documentReset);

    doc.setLines({QStringLiteral("a"), QStringLiteral("bb"),
                  QStringLiteral("ccc")});

    QCOMPARE(doc.lineCount(), 3);
    QCOMPARE(doc.lineAt(0), QStringLiteral("a"));
    QCOMPARE(doc.lineAt(1), QStringLiteral("bb"));
    QCOMPARE(doc.lineAt(2), QStringLiteral("ccc"));
    QCOMPARE(spy.count(), 1);
}

void TestSimpleDocument::lineAt_outOfRange_returnsEmpty() {
    qce::SimpleTextDocument doc;
    doc.setLines({QStringLiteral("only")});

    QCOMPARE(doc.lineAt(-1), QString());
    QCOMPARE(doc.lineAt(1), QString());
    QCOMPARE(doc.lineAt(100), QString());
}

void TestSimpleDocument::setText_splitsOnLF() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("first\nsecond\nthird"));

    QCOMPARE(doc.lineCount(), 3);
    QCOMPARE(doc.lineAt(0), QStringLiteral("first"));
    QCOMPARE(doc.lineAt(1), QStringLiteral("second"));
    QCOMPARE(doc.lineAt(2), QStringLiteral("third"));
}

void TestSimpleDocument::setText_normalizesCRLF() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("a\r\nb\r\nc"));

    QCOMPARE(doc.lineCount(), 3);
    QCOMPARE(doc.lineAt(0), QStringLiteral("a"));
    QCOMPARE(doc.lineAt(1), QStringLiteral("b"));
    QCOMPARE(doc.lineAt(2), QStringLiteral("c"));
}

void TestSimpleDocument::setText_emptyString_givesZeroLines() {
    qce::SimpleTextDocument doc;
    doc.setText(QString());
    QCOMPARE(doc.lineCount(), 0);
}

void TestSimpleDocument::setText_singleNewline_givesOneEmptyLine() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("\n"));
    // "\n" means "one empty line, terminated".
    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QString());
}

void TestSimpleDocument::setText_trailingNewline_doesNotCreateExtraEmpty() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abc\n"));
    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QStringLiteral("abc"));
}

void TestSimpleDocument::toPlainText_roundTrip() {
    qce::SimpleTextDocument doc;
    const QString input = QStringLiteral("one\ntwo\nthree");
    doc.setText(input);
    QCOMPARE(doc.toPlainText(), input);
}

void TestSimpleDocument::maxLineLength_emptyDocument_isZero() {
    qce::SimpleTextDocument doc;
    QCOMPARE(doc.maxLineLength(), 0);
}

void TestSimpleDocument::maxLineLength_computesAndCaches() {
    qce::SimpleTextDocument doc;
    doc.setLines({QStringLiteral("a"), QStringLiteral("twelve chars"),
                  QStringLiteral("hi")});
    QCOMPARE(doc.maxLineLength(), 12);
    // Second call hits the cache; result must be identical.
    QCOMPARE(doc.maxLineLength(), 12);
}

void TestSimpleDocument::maxLineLength_invalidatesAfterSetLines() {
    qce::SimpleTextDocument doc;
    doc.setLines({QStringLiteral("long line here")});
    QCOMPARE(doc.maxLineLength(), 14);

    doc.setLines({QStringLiteral("x")});
    QCOMPARE(doc.maxLineLength(), 1);
}

void TestSimpleDocument::insertText_singleLine_inMiddle() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello world"));

    doc.insertText({0, 5}, QStringLiteral(" beautiful"));

    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QStringLiteral("hello beautiful world"));
}

void TestSimpleDocument::insertText_multiLine() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abcdef"));

    doc.insertText({0, 3}, QStringLiteral("X\nY"));

    QCOMPARE(doc.lineCount(), 2);
    QCOMPARE(doc.lineAt(0), QStringLiteral("abcX"));
    QCOMPARE(doc.lineAt(1), QStringLiteral("Ydef"));
}

void TestSimpleDocument::insertText_atEnd_appendsText() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("foo"));

    doc.insertText({0, 3}, QStringLiteral("bar"));

    QCOMPARE(doc.lineAt(0), QStringLiteral("foobar"));
}

void TestSimpleDocument::insertText_returnsCorrectCursor() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abcdef"));

    // Single-line: inserted "XYZ" at column 2 → cursor lands at column 5.
    auto pos = doc.insertText({0, 2}, QStringLiteral("XYZ"));
    QCOMPARE(pos.line, 0);
    QCOMPARE(pos.column, 5);

    // Multi-line: inserted "P\nQ" → cursor on new line, column = length of "Q".
    qce::SimpleTextDocument doc2;
    doc2.setText(QStringLiteral("start"));
    auto pos2 = doc2.insertText({0, 0}, QStringLiteral("P\nQ"));
    QCOMPARE(pos2.line, 1);
    QCOMPARE(pos2.column, 1);
}

void TestSimpleDocument::insertText_emitsLinesChanged() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello"));
    QSignalSpy spy(&doc, &qce::ITextDocument::linesChanged);

    doc.insertText({0, 2}, QStringLiteral("XY"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0); // startLine
    QCOMPARE(spy.at(0).at(1).toInt(), 1); // count
}

void TestSimpleDocument::insertText_emitsLinesInserted_forMultiLine() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello"));
    QSignalSpy spyChanged(&doc, &qce::ITextDocument::linesChanged);
    QSignalSpy spyInserted(&doc, &qce::ITextDocument::linesInserted);

    doc.insertText({0, 2}, QStringLiteral("A\nB\nC"));

    QCOMPARE(spyChanged.count(), 1);
    QCOMPARE(spyInserted.count(), 1);
    QCOMPARE(spyInserted.at(0).at(1).toInt(), 2); // inserted 2 new lines
}

void TestSimpleDocument::removeText_singleLine_inMiddle() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello world"));

    doc.removeText({0, 5}, {0, 11});

    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QStringLiteral("hello"));
}

void TestSimpleDocument::removeText_multiLine() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("line0\nline1\nline2"));

    doc.removeText({0, 4}, {2, 4});

    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QStringLiteral("line2"));
}

void TestSimpleDocument::removeText_samePosition_returnsEmpty() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abc"));

    const QString removed = doc.removeText({0, 1}, {0, 1});

    QCOMPARE(removed, QString());
    QCOMPARE(doc.lineAt(0), QStringLiteral("abc")); // unchanged
}

void TestSimpleDocument::removeText_returnsRemovedText() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abcdef\nghijkl"));

    const QString removed = doc.removeText({0, 3}, {1, 3});

    QCOMPARE(removed, QStringLiteral("def\nghi"));
    QCOMPARE(doc.lineCount(), 1);
    QCOMPARE(doc.lineAt(0), QStringLiteral("abcjkl"));
}

void TestSimpleDocument::removeText_emitsLinesChanged() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("abcdef"));
    QSignalSpy spy(&doc, &qce::ITextDocument::linesChanged);

    doc.removeText({0, 1}, {0, 4});

    QCOMPARE(spy.count(), 1);
}

void TestSimpleDocument::removeText_emitsLinesRemoved_forMultiLine() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("a\nb\nc\nd"));
    QSignalSpy spyRemoved(&doc, &qce::ITextDocument::linesRemoved);
    QSignalSpy spyChanged(&doc, &qce::ITextDocument::linesChanged);

    doc.removeText({0, 1}, {2, 0});

    QCOMPARE(spyRemoved.count(), 1);
    QCOMPARE(spyRemoved.at(0).at(1).toInt(), 2); // removed 2 lines
    QCOMPARE(spyChanged.count(), 1);
}

void TestSimpleDocument::stripTrailingWhitespace_removesSpacesAndTabs() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("hello   \nworld\t\nfoo"));

    doc.stripTrailingWhitespace();

    QCOMPARE(doc.lineAt(0), QStringLiteral("hello"));
    QCOMPARE(doc.lineAt(1), QStringLiteral("world"));
    QCOMPARE(doc.lineAt(2), QStringLiteral("foo")); // no trailing ws
}

void TestSimpleDocument::stripTrailingWhitespace_leavesCleanLinesUntouched() {
    qce::SimpleTextDocument doc;
    doc.setText(QStringLiteral("clean\nlines\nhere"));
    QSignalSpy spy(&doc, &qce::ITextDocument::linesChanged);

    doc.stripTrailingWhitespace();

    QCOMPARE(spy.count(), 0); // no signals for unchanged lines
    QCOMPARE(doc.toPlainText(), QStringLiteral("clean\nlines\nhere"));
}

QTEST_APPLESS_MAIN(TestSimpleDocument)
#include "test_simple_document.moc"
