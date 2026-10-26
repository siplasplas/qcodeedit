#include "LineRenderer.h"

#include <QImage>
#include <QPainter>
#include <QtTest/QtTest>

#include <functional>

class TestLineRenderer : public QObject {
    Q_OBJECT
private slots:
    void visualColumn_noTabs();
    void visualColumn_singleTab_atStart();
    void visualColumn_tabInMiddle();
    void visualColumn_multipleTabs();
    void visualColumn_pastEnd_returnsEndVisual();

    // Surrogate pairs (characters outside the BMP) are one column
    void visualColumn_surrogatePair_isOneColumn();
    void cellCount_countsCodePoints();
    void drawCells_keepsTextAfterPairOnTheGrid();

    // Wide characters (Chinese, ...) take two cells
    void visualColumn_wideCharacter_isTwoCells();
    void drawCells_keepsTextAfterWideOnTheGrid();
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

// "ab" + two Egyptian hieroglyphs (U+13032, a surrogate pair each) + "cd":
// UTF-16 indices a=0 b=1 [2,3] [4,5] c=6 d=7, cells 0 1 2 3 4 5.
static const QString kPairLine = QString::fromUcs4(U"ab\U00013032\U00013032cd");

void TestLineRenderer::visualColumn_surrogatePair_isOneColumn() {
    QCOMPARE(LineRenderer::visualColumn(kPairLine, 2, 4), 2);
    QCOMPARE(LineRenderer::visualColumn(kPairLine, 4, 4), 3);
    QCOMPARE(LineRenderer::visualColumn(kPairLine, 6, 4), 4);
    QCOMPARE(LineRenderer::visualColumn(kPairLine, 8, 4), 6);
    // With a tab before it: "\t" + pair, tabWidth 4 → pair at cell 4, then cell 5.
    const QString tabbed = QStringLiteral("\t") + kPairLine.mid(2, 2);
    QCOMPARE(LineRenderer::visualColumn(tabbed, 3, 4), 5);
}

void TestLineRenderer::cellCount_countsCodePoints() {
    QCOMPARE(LineRenderer::cellCount(QStringLiteral("abcd")), 4);
    QCOMPARE(LineRenderer::cellCount(kPairLine), 6);
    QCOMPARE(LineRenderer::cellCount(QString()), 0);
}

// The hieroglyph comes from a fallback font whose advance differs from the
// cell width; drawn as one run it would shift "cd". Drawn by cells, the
// pixels from the cell after "c" on are the same as for a plain line.
void TestLineRenderer::drawCells_keepsTextAfterPairOnTheGrid() {
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    const QFontMetrics fm(font);
    const int cw = fm.horizontalAdvance(QLatin1Char('M'));
    const int baseline = fm.ascent();
    const int height = fm.height();
    const auto render = [&](const std::function<void(QPainter&)>& draw) {
        QImage img(cw * 12, height, QImage::Format_RGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setFont(font);
        p.setPen(Qt::black);
        draw(p);
        return img;
    };
    const QImage pairs = render([&](QPainter& p) {
        LineRenderer::drawCells(p, 0, baseline, cw, kPairLine + QStringLiteral("ef"));
    });
    // The same runs drawn by hand at their cells (a single run would place
    // "cdef" by the font's fractional advances, not by cells).
    const QImage plain = render([&](QPainter& p) {
        p.drawText(0, baseline, QStringLiteral("ab"));
        p.drawText(4 * cw, baseline, QStringLiteral("cdef"));
    });
    // "ef" is at cells 6-7. The hieroglyph glyphs (cells 2-3) may be up to
    // twice as wide as a cell and spill into "cd", so compare from cell 6 on.
    const auto samePixels = [&](int fromX, int toX) {
        for (int x = fromX; x < toX; ++x)
            for (int y = 0; y < pairs.height(); ++y)
                if (pairs.pixel(x, y) != plain.pixel(x, y))
                    return QStringLiteral("pixel (%1, %2) differs, cell width %3").arg(x).arg(y).arg(cw);
        return QString();
    };
    QVERIFY2(samePixels(6 * cw, pairs.width()).isEmpty(), qPrintable(samePixels(6 * cw, pairs.width())));
    // And "ab" (cells 0-1) is untouched by the pairs after it.
    QVERIFY2(samePixels(0, 2 * cw).isEmpty(), qPrintable(samePixels(0, 2 * cw)));
}

// "a" + 詞源 (etymology, U+8A5E U+6E90, wide) + "b": cells a=0, 詞 1-2, 源 3-4, b=5.
static const QString kWideLine = QString::fromUcs4(U"a\u8A5E\u6E90b");

void TestLineRenderer::visualColumn_wideCharacter_isTwoCells() {
    QCOMPARE(LineRenderer::visualColumn(kWideLine, 1, 4), 1);
    QCOMPARE(LineRenderer::visualColumn(kWideLine, 2, 4), 3);
    QCOMPARE(LineRenderer::visualColumn(kWideLine, 3, 4), 5);
    QCOMPARE(LineRenderer::cellCount(kWideLine), 6);
    // A wide character outside the BMP (CJK Extension B, U+20000): two cells, one code point.
    const QString extension = QString::fromUcs4(U"a\U00020000b");
    QCOMPARE(LineRenderer::visualColumn(extension, 3, 4), 3);
    QCOMPARE(LineRenderer::cellCount(extension), 4);
}

// Drawn as one run, the wide glyphs would place "b" by their own advances,
// off the caret's grid; drawn alone at their cells, "b" is at cell 5.
void TestLineRenderer::drawCells_keepsTextAfterWideOnTheGrid() {
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::Monospace);
    const QFontMetrics fm(font);
    const int cw = fm.horizontalAdvance(QLatin1Char('M'));
    const int baseline = fm.ascent();
    const auto render = [&](const std::function<void(QPainter&)>& draw) {
        QImage img(cw * 10, fm.height(), QImage::Format_RGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        p.setFont(font);
        p.setPen(Qt::black);
        draw(p);
        return img;
    };
    const QImage wide = render([&](QPainter& p) { LineRenderer::drawCells(p, 0, baseline, cw, kWideLine + QStringLiteral("cd")); });
    const QImage plain = render([&](QPainter& p) {
        p.drawText(0, baseline, QStringLiteral("a"));
        p.drawText(5 * cw, baseline, QStringLiteral("bcd"));
    });
    // From cell 6 on ("cd"), and cell 0 ("a"), the pixels are the same.
    for (const auto& [from, to] : {std::pair{6 * cw, int(wide.width())}, {0, cw}})
        for (int x = from; x < to; ++x)
            for (int y = 0; y < wide.height(); ++y)
                QVERIFY2(wide.pixel(x, y) == plain.pixel(x, y), qPrintable(QStringLiteral("pixel (%1, %2)").arg(x).arg(y)));
}

QTEST_MAIN(TestLineRenderer)
#include "test_line_renderer.moc"
