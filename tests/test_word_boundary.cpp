#include <qce/WordBoundary.h>

#include <QRegularExpression>
#include <QtTest/QtTest>

using namespace qce::words;

// The shared word definition: cursor stops, whole-word checks and the
// whole-word regular expression must agree.
class TestWordBoundary : public QObject {
    Q_OBJECT

private:
    static QList<int> stopsForward(const QString& line) {
        QList<int> stops;
        for (int col = 0; col < line.size();) {
            col = nextWordStop(line, col);
            stops << col;
        }
        return stops;
    }

    static QList<int> stopsBackward(const QString& line) {
        QList<int> stops;
        for (int col = int(line.size()); col > 0;) {
            col = previousWordStop(line, col);
            stops << col;
        }
        return stops;
    }

private slots:
    void wordCharacters() {
        const QString s = QStringLiteral("aZ_9ąé́ .-()");
        for (int i = 0; i < 7; ++i) QVERIFY2(isWordCharAt(s, i), qPrintable(QString::number(i)));
        for (int i = 7; i < s.size(); ++i) QVERIFY2(!isWordCharAt(s, i), qPrintable(QString::number(i)));
        QVERIFY(!isWordCharAt(s, -1));
        QVERIFY(!isWordCharAt(s, int(s.size())));
    }

    void surrogatePairsUseTheirCodePoint() {
        // U+1D400 MATHEMATICAL BOLD CAPITAL A (a letter), U+1F600 (a symbol).
        const QString s = QString::fromUcs4(U"x\U0001D400 \U0001F600");
        QVERIFY(isWordCharAt(s, 1));
        QVERIFY(isWordCharAt(s, 2));
        QVERIFY(!isWordCharAt(s, 4));
        QCOMPARE(nextWordStop(s, 0), 4);
    }

    void stopsInCode() {
        const QString line = QStringLiteral("obj.method(x) += 1;");
        QCOMPARE(stopsForward(line), (QList<int>{3, 4, 10, 11, 12, 14, 17, 18, 19}));
        QCOMPARE(stopsBackward(line), (QList<int>{18, 17, 14, 12, 11, 10, 4, 3, 0}));
    }

    void stopsInText() {
        const QString line = QStringLiteral("  Zażółć gęślą, jaźń");
        QCOMPARE(stopsForward(line), (QList<int>{2, 9, 14, 16, 20}));
        QCOMPARE(stopsBackward(line), (QList<int>{16, 14, 9, 2, 0}));
    }

    void wholeWord() {
        const QString text = QStringLiteral("foo foo_bar foo.bar żfoo");
        QVERIFY(isWholeWord(text, 0, 3));    // "foo"
        QVERIFY(!isWholeWord(text, 4, 7));   // "foo" in "foo_bar"
        QVERIFY(isWholeWord(text, 12, 15));  // "foo" before "."
        QVERIFY(isWholeWord(text, 16, 19));  // "bar" after "."
        QVERIFY(!isWholeWord(text, 21, 24)); // "foo" after "ż"
    }

    void regexAgreesWithIsWholeWord() {
        const QString text = QStringLiteral("foo foo_bar foo.bar żfoo é́foo foo9 (foo)");
        const QRegularExpression re(wholeWordPattern(QStringLiteral("foo")),
                                    QRegularExpression::UseUnicodePropertiesOption);
        QVERIFY(re.isValid());
        QList<int> viaRegex;
        for (auto it = re.globalMatch(text); it.hasNext();) viaRegex << int(it.next().capturedStart());
        QList<int> viaFunction;
        for (int i = text.indexOf(QStringLiteral("foo")); i >= 0;
             i = text.indexOf(QStringLiteral("foo"), i + 1))
            if (isWholeWord(text, i, i + 3)) viaFunction << i;
        QCOMPARE(viaRegex, viaFunction);
        QCOMPARE(viaRegex.size(), 3); // "foo", "foo" in "foo.bar", "(foo)"
    }
};

QTEST_APPLESS_MAIN(TestWordBoundary)
#include "test_word_boundary.moc"
