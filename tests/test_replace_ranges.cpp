// CodeEditArea::replaceRanges(): many replacements at once, one undo step.

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>

#include <QElapsedTimer>
#include <QUndoStack>
#include <QtTest>

using namespace qce;

class TestReplaceRanges : public QObject {
    Q_OBJECT

    // Every occurrence of `what` in the document, as ranges to replace with `with`.
    static QList<CodeEditArea::TextReplacement> occurrences(const ITextDocument& doc, const QString& what,
                                                            const QString& with) {
        QList<CodeEditArea::TextReplacement> result;
        for (int line = 0; line < doc.lineCount(); ++line) {
            const QString text = doc.lineAt(line);
            for (qsizetype at = text.indexOf(what); at >= 0; at = text.indexOf(what, at + what.size()))
                result.append({{line, int(at)}, {line, int(at + what.size())}, with});
        }
        return result;
    }

private slots:
    void longerAndShorter() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("a \"b\" c\nnone\n\"x\"\"y\""));
        CodeEditArea area;
        area.setDocument(&doc);
        QVERIFY(area.replaceRanges(occurrences(doc, QStringLiteral("\""), QStringLiteral("&quot;")),
                                   QStringLiteral("Replace All")));
        QCOMPARE(doc.toPlainText(), QStringLiteral("a &quot;b&quot; c\nnone\n&quot;x&quot;&quot;y&quot;"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 8}));  // after the first replacement
        QCOMPARE(area.undoStack()->count(), 1);
        QCOMPARE(area.undoStack()->undoText(), QStringLiteral("Replace All"));
        area.undoStack()->undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("a \"b\" c\nnone\n\"x\"\"y\""));
        area.undoStack()->redo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("a &quot;b&quot; c\nnone\n&quot;x&quot;&quot;y&quot;"));
        QVERIFY(area.replaceRanges(occurrences(doc, QStringLiteral("&quot;"), QStringLiteral("'"))));
        QCOMPARE(doc.toPlainText(), QStringLiteral("a 'b' c\nnone\n'x''y'"));
    }

    void acrossLines() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("one;two\nthree"));
        CodeEditArea area;
        area.setDocument(&doc);
        // A line break in, and one out (the end of line 0 to the start of line 1).
        QVERIFY(area.replaceRanges({{{0, 3}, {0, 4}, QStringLiteral("\n")}, {{0, 7}, {1, 0}, QStringLiteral(" ")}}));
        QCOMPARE(doc.toPlainText(), QStringLiteral("one\ntwo three"));
        QCOMPARE(area.cursorPosition(), (TextCursor{1, 0}));
        area.undoStack()->undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("one;two\nthree"));
    }

    void readOnlyOrNothing() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.setDocument(&doc);
        QVERIFY(!area.replaceRanges({}));
        area.setReadOnly(true);
        QVERIFY(!area.replaceRanges({{{0, 0}, {0, 1}, QStringLiteral("x")}}));
        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
    }

    // Long lines with many replacements: linear, not one edit a replacement.
    void longLines() {
        QStringList lines;
        for (int i = 0; i < 50; ++i) lines << QStringLiteral("{\"k\": \"v\"}, ").repeated(2500);  // 30 000 characters
        SimpleTextDocument doc;
        doc.setText(lines.join(QLatin1Char('\n')));
        CodeEditArea area;
        area.setDocument(&doc);
        const auto ranges = occurrences(doc, QStringLiteral("\""), QStringLiteral("&quot;"));
        QCOMPARE(ranges.size(), 50 * 2500 * 4);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(area.replaceRanges(ranges));
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(doc.lineAt(49), QStringLiteral("{&quot;k&quot;: &quot;v&quot;}, ").repeated(2500));
    }
};

QTEST_MAIN(TestReplaceRanges)
#include "test_replace_ranges.moc"
