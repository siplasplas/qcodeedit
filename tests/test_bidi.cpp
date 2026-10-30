#include "Bidi.h"

#include <QtTest/QtTest>

class TestBidi : public QObject {
    Q_OBJECT
private slots:
    void latin_noRightToLeft();
    void hebrewInLatin_wordsReversed();
    void hebrewParagraph_periodOnTheLeft();
    void numberInHebrew_staysLeftToRight();
    void latinInHebrew_staysLeftToRight();
    void trailingWhitespace_atParagraphLevel();
    void tab_atParagraphLevel();
    void arabicDigitsAfterArabic_areArabicNumbers();
    void row_reorderedOnItsOwn();
    void surrogatePair_oneEntry();
};

using namespace qce;

namespace {

QVector<int> order(const QString& line, int from = 0, int to = -1) {
    if (to < 0) to = int(line.size());
    return bidi::visualOrder(line, bidi::levels(line), from, to);
}

QVector<int> range(int from, int to) {  // from..to, either way, inclusive
    QVector<int> result;
    for (int i = from;; i += from <= to ? 1 : -1) {
        result << i;
        if (i == to) break;
    }
    return result;
}

} // namespace

void TestBidi::latin_noRightToLeft() {
    const QString line = QStringLiteral("int x = 1; // Zażółć, Привет");
    QVERIFY(!bidi::hasRightToLeft(line));
    QCOMPARE(order(line), range(0, int(line.size()) - 1));
    QCOMPARE(bidi::levels(line).paragraph, 0);
}

// "abc שלום עולם def": the Hebrew words and the space between them read right
// to left; the spaces between Latin and Hebrew go with the paragraph (left
// to right).
void TestBidi::hebrewInLatin_wordsReversed() {
    const QString line = QStringLiteral("abc שלום עולם def");
    QVERIFY(bidi::hasRightToLeft(line));
    QVERIFY(!bidi::hasRightToLeft(line, 0, 4));
    QCOMPARE(bidi::levels(line).paragraph, 0);
    QCOMPARE(order(line), range(0, 3) + range(12, 4) + range(13, 16));
}

// A line that starts with Hebrew is a right-to-left paragraph: the period
// after the last word stands left of it.
void TestBidi::hebrewParagraph_periodOnTheLeft() {
    const QString line = QStringLiteral("שלום עולם.");
    QCOMPARE(bidi::levels(line).paragraph, 1);
    QCOMPARE(order(line), range(9, 0));
}

// "אב 123 גד": the number reads left to right among the Hebrew words.
void TestBidi::numberInHebrew_staysLeftToRight() {
    const QString line = QStringLiteral("אב 123 גד");
    QCOMPARE(order(line), range(8, 6) + range(3, 5) + range(2, 0));
    // "5%" and "1,5": a terminator and a separator go with the number.
    const QString more = QStringLiteral("א 5% ב 1,5");
    QCOMPARE(order(more), range(7, 9) + range(6, 4) + range(2, 3) + range(1, 0));
}

// Latin words in a Hebrew paragraph keep their order, and so does the space
// between them.
void TestBidi::latinInHebrew_staysLeftToRight() {
    const QString line = QStringLiteral("אב Qt Creator גד");
    QCOMPARE(order(line), range(15, 13) + range(3, 12) + range(2, 0));
}

void TestBidi::trailingWhitespace_atParagraphLevel() {
    // Left-to-right paragraph: the spaces after the Hebrew word stay at the end.
    const QString line = QStringLiteral("abc שלום  ");
    QCOMPARE(order(line), range(0, 3) + range(7, 4) + range(8, 9));
}

void TestBidi::tab_atParagraphLevel() {
    // A tab between two Hebrew words in a left-to-right paragraph splits them
    // into two runs, each read right to left.
    const QString line = QStringLiteral("a אב\tגד");
    QCOMPARE(order(line), range(0, 1) + range(3, 2) + QVector<int>{4} + range(6, 5));
}

// "12" after Arabic letters is an Arabic number: left to right inside the
// right-to-left text.
void TestBidi::arabicDigitsAfterArabic_areArabicNumbers() {
    const QString line = QStringLiteral("عدد 12");
    QCOMPARE(bidi::levels(line).paragraph, 1);
    QCOMPARE(bidi::levels(line).levels[4], quint8(2));
    QCOMPARE(order(line), range(4, 5) + range(3, 0));
}

// A wrapped row is reordered on its own, with the levels of the whole line.
void TestBidi::row_reorderedOnItsOwn() {
    const QString line = QStringLiteral("abc שלום עולם def");
    // "שלום ": the space ends the row, so it is at the paragraph's level.
    QCOMPARE(order(line, 4, 9), range(7, 4) + QVector<int>{8});
    QCOMPARE(order(line, 9, 17), range(12, 9) + range(13, 16));
}

// A character outside the BMP (U+10900, Phoenician, right to left) is one
// entry, at the index of its first half.
void TestBidi::surrogatePair_oneEntry() {
    const QString line = QString::fromUcs4(U"ab \U00010900\U00010901");
    QVERIFY(bidi::hasRightToLeft(line));
    QCOMPARE(order(line), (QVector<int>{0, 1, 2, 5, 3}));
}

QTEST_APPLESS_MAIN(TestBidi)
#include "test_bidi.moc"
