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

QTEST_APPLESS_MAIN(TestSimpleDocument)
#include "test_simple_document.moc"
