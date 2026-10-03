#include <QtTest>
#include "LineRenderer.h"
#include <QScrollBar>
#include <QUndoStack>
#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/IHighlighter.h>
#include <qce/IFoldingProvider.h>

using namespace qce;

class CountingHighlighter : public IHighlighter {
public:
    mutable int calls = 0;
    QVector<TextAttribute> attrs{{QColor(Qt::blue), QColor(Qt::cyan), true, true, true}};
    HighlightState initialState() const override { return {{0}, {QStringList{}}}; }
    void highlightLine(const QString& text, const HighlightState& in,
                       QVector<StyleSpan>& spans, HighlightState& out) const override {
        ++calls;
        spans = {{0, int(text.size()), 0}};
        out = in;
    }
    const QVector<TextAttribute>& attributes() const override { return attrs; }
};

class CountingFolds : public IFoldingProvider {
public:
    mutable int calls = 0;
    QVector<FoldRegion> computeRegions(const ITextDocument*) const override {
        ++calls;
        FoldRegion fold;
        fold.startLine = 0; fold.startColumn = 3;
        fold.endLine = 2; fold.endColumn = 4;
        return {fold};
    }
};

class TestExtraSelections : public QObject {
    Q_OBJECT
    inline static QFont renderFont;
    static QImage render(CodeEditArea& area) {
        renderFont = area.font();
        QImage result(area.viewport()->size(), QImage::Format_ARGB32_Premultiplied);
        result.fill(Qt::transparent);
        area.viewport()->render(&result);
        return result;
    }
    // Sample the bottom of the centered background band, below the glyphs.
    static QColor cell(const QImage& image, const ViewportState& vp, int col, int row = 0) {
        const auto band = LineRenderer::backgroundBand(renderFont, vp.lineHeight);
        return image.pixelColor(4 + col * vp.charWidth + vp.charWidth / 2,
                                row * vp.lineHeight + band.offset + band.height - 2);
    }
    static void show(CodeEditArea& area) {
        area.resize(340, 180);
        area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&area));
    }
private slots:
    void selectionDirectionSignalsAndEditing() {
        SimpleTextDocument doc;
        doc.setText("abcdef\nsecond");
        CodeEditArea area;
        area.setDocument(&doc);
        show(area);
        QSignalSpy cursor(&area, &CodeEditArea::cursorPositionChanged);
        QSignalSpy selection(&area, &CodeEditArea::selectionChanged);
        area.setSelection({0, 1}, {0, 4});
        QCOMPARE(area.selectedText(), QString("bcd"));
        QCOMPARE(cursor.count(), 1);
        QCOMPARE(selection.count(), 1);
        area.setSelection({0, 1}, {0, 4});
        QCOMPARE(selection.count(), 1);
        area.setSelection({0, 4}, {0, 1});
        QCOMPARE(area.selectionStart(), (TextCursor{0, 1}));
        QCOMPARE(area.selectionEnd(), (TextCursor{0, 4}));
        QTest::keyClick(&area, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(area.selectedText(), QString("cd"));
        area.setSelection({0, 1}, {0, 4});
        QTest::keyClicks(&area, "X");
        QCOMPARE(doc.toPlainText(), QString("aXef\nsecond"));
        area.undo();
        QCOMPARE(doc.toPlainText(), QString("abcdef\nsecond"));
        area.setSelection({-5, -2}, {99, 99});
        QCOMPARE(area.selectionStart(), (TextCursor{0, 0}));
        QCOMPARE(area.selectionEnd(), (TextCursor{1, 6}));
        area.setSelection({0, 2}, {0, 2});
        QVERIFY(!area.hasSelection());
        SimpleTextDocument empty;
        area.setDocument(&empty);
        area.setSelection({2, 5}, {-1, 2});
        QVERIFY(!area.hasSelection());
        QCOMPARE(area.cursorPosition(), (TextCursor{}));
        area.setDocument(nullptr);
        area.setSelection({2, 5}, {8, 8});
        QVERIFY(!area.hasSelection());
    }
    void selectionScrollsToActiveEnd() {
        SimpleTextDocument doc;
        QStringList lines;
        for (int i = 0; i < 100; ++i) lines.append(QString(160, 'x'));
        doc.setLines(lines);
        CodeEditArea area;
        area.setDocument(&doc);
        show(area);
        area.setSelection({0, 0}, {90, 150});
        QVERIFY(area.viewportState().firstVisibleLine <= 90);
        QVERIFY(area.viewportState().lastVisibleLine >= 90);
        QVERIFY(area.horizontalScrollBar()->value() > 0);
        area.setSelection({90, 150}, {0, 0});
        QCOMPARE(area.verticalScrollBar()->value(), 0);
        QCOMPARE(area.horizontalScrollBar()->value(), 0);
    }
    void overlapMultilineAndTheme_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void overlapMultilineAndTheme() {
        QFETCH(bool, dark);
        SimpleTextDocument doc;
        doc.setText("abcdef\nghijkl\nmnopqr");
        CodeEditArea area;
        area.setDocument(&doc);
        QPalette pal = area.palette();
        pal.setColor(QPalette::Base, dark ? Qt::black : Qt::white);
        pal.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
        area.setPalette(pal);
        area.viewport()->setPalette(pal);
        show(area);
        area.setLineBackgroundProvider([](int) { return QColor(Qt::green); });
        const auto before = render(area);
        area.setExtraSelections({{{0, 1}, {2, 2}, Qt::yellow, {}},
                                 {{0, 3}, {0, 5}, Qt::red, {}}});
        auto vp = area.viewportState();
        auto image = render(area);
        QCOMPARE(cell(image, vp, 1), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 3), QColor(Qt::red));
        QCOMPARE(cell(image, vp, 5), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 0, 1), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 1, 2), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 2, 2), cell(before, vp, 2, 2));
        QCOMPARE(cell(image, vp, 8, 1), cell(before, vp, 8, 1));
        area.setSelection({0, 2}, {0, 4});
        image = render(area);
        QCOMPARE(cell(image, vp, 3), area.selectionColor());
        area.setCursorPosition({0, 0});
        area.setExtraSelections({});
        QCOMPARE(render(area), before);
    }
    void syntaxAndFoldsAreIndependent() {
        SimpleTextDocument doc;
        doc.setText("abcdef\nbody\nend\nafter");
        CountingHighlighter syntax;
        CountingFolds folds;
        CodeEditArea area;
        area.setDocument(&doc);
        area.setHighlighter(&syntax);
        area.setFoldingProvider(&folds);
        area.setWordWrap(true);
        show(area);
        area.toggleFoldAt(0);
        const int highlights = syntax.calls, folding = folds.calls;
        const int undoCount = area.undoStack()->count();
        const bool clean = area.undoStack()->isClean();
        const auto before = render(area);
        const auto cursor = area.cursorPosition();
        QSignalSpy cursorSignals(&area, &CodeEditArea::cursorPositionChanged);
        QSignalSpy selectionSignals(&area, &CodeEditArea::selectionChanged);
        area.setExtraSelections({{{0, 1}, {3, 2}, Qt::yellow, {}}});
        auto image = render(area);
        const auto vp = area.viewportState();
        QCOMPARE(cell(image, vp, 1), QColor(Qt::yellow));
        // Placeholder and hidden text are not decorated.
        QCOMPARE(cell(image, vp, 5), cell(before, vp, 5));
        QCOMPARE(cell(image, vp, 0, 1), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 2, 1), QColor(Qt::cyan));
        QVERIFY(area.foldState().isCollapsed(0));
        QCOMPARE(syntax.calls, highlights);
        QCOMPARE(folds.calls, folding);
        QCOMPARE(area.cursorPosition(), cursor);
        QCOMPARE(cursorSignals.count(), 0);
        QCOMPARE(selectionSignals.count(), 0);
        QVERIFY(!area.hasSelection());
        // Ignore antialiasing differences caused by drawing separate text runs.
        int bluePixels = 0;
        for (int y = 0; y < vp.lineHeight; ++y)
            for (int x = 4 + vp.charWidth; x < 4 + 3 * vp.charWidth; ++x) {
                const QColor ink = image.pixelColor(x, y);
                if (ink.blue() > 240 && ink.red() < 15 && ink.green() < 15) ++bluePixels;
            }
        QVERIFY(bluePixels > 0);
        QCOMPARE(area.undoStack()->count(), undoCount);
        QCOMPARE(area.undoStack()->isClean(), clean);
        QCOMPARE(doc.toPlainText(), QString("abcdef\nbody\nend\nafter"));
        // Background priority also applies above syntax backgrounds.
        area.setSelection({0, 0}, {0, 2});
        QCOMPARE(cell(render(area), vp, 1), area.selectionColor());
        area.setCursorPosition({0, 0});
        area.setExtraSelections({});
        QCOMPARE(render(area), before);
        // Explicit foreground affects glyphs, while font styling is retained.
        area.setExtraSelections({{{0, 0}, {0, 3}, {}, Qt::red}});
        image = render(area);
        int redPixels = 0;
        for (int y = 0; y < vp.lineHeight; ++y)
            for (int x = 4; x < 4 + 3 * vp.charWidth; ++x) {
                QColor color = image.pixelColor(x, y);
                if (color.red() > 150 && color.blue() < 100) ++redPixels;
            }
        QVERIFY(redPixels > 0);
        QCOMPARE(syntax.calls, highlights);
    }
    void tabsUnicodeWrapAndHorizontalScroll() {
        SimpleTextDocument doc;
        doc.setText(QString::fromUtf8("a\t😀bcdefghijklmnopqrstuvwxyz0123456789"));
        CodeEditArea area;
        area.setDocument(&doc);
        show(area);
        area.setExtraSelections({{{0, 1}, {0, 4}, Qt::yellow, {}}});
        auto vp = area.viewportState();
        auto image = render(area);
        // Tab is UTF-16 column 1; emoji occupies columns 2 and 3.
        for (int col = 1; col < 6; ++col) QCOMPARE(cell(image, vp, col), QColor(Qt::yellow));
        QVERIFY(cell(image, vp, 6) != QColor(Qt::yellow));
        area.horizontalScrollBar()->setValue(2);
        vp = area.viewportState();
        image = render(area);
        QCOMPARE(image.pixelColor(4 + 3 * vp.charWidth - vp.contentOffsetX,
                                   LineRenderer::backgroundBand(area.font(), vp.lineHeight).offset
                                   + LineRenderer::backgroundBand(area.font(), vp.lineHeight).height - 2), QColor(Qt::yellow));
        area.setWordWrap(true);
        area.resize(125, 250);
        QCoreApplication::processEvents();
        area.setExtraSelections({{{0, 4}, {0, 28}, Qt::yellow, {}}});
        vp = area.viewportState();
        image = render(area);
        QVERIFY(vp.rows.size() > 1);
        for (int r = 0; r < vp.rows.size(); ++r) {
            const auto row = vp.rows[r];
            for (int col = row.startCol; col < row.endCol; ++col) {
                const int visual = (col - row.startCol) + (row.startCol == 0 && col >= 2 ? 2 : 0);
                if (col >= 4 && col < 28) QCOMPARE(cell(image, vp, visual, r), QColor(Qt::yellow));
            }
        }
    }
    void rangeBackgroundsMatchWholeLineBand() {
        for (bool wrap : {false, true}) {
            SimpleTextDocument doc;
            doc.setText("LLL\nLLL\nLLL");
            CodeEditArea area;
            area.setDocument(&doc);
            area.setWordWrap(wrap);
            show(area);
            area.setLineBackgroundProvider([](int line) {
                return line == 1 ? QColor(Qt::yellow) : QColor{};
            });
            const QImage wholeLine = render(area);
            const auto vp = area.viewportState();
            const QRect stripe(4 + vp.charWidth, 0, vp.charWidth, wholeLine.height());
            area.setLineBackgroundProvider({});
            area.setSelectionColor(Qt::yellow);
            area.setSelection({1, 0}, {1, 3});
            QCOMPARE(render(area).copy(stripe), wholeLine.copy(stripe));
            area.setCursorPosition({0, 0});
            area.setExtraSelections({{{1, 0}, {1, 3}, Qt::yellow, {}}});
            QCOMPARE(render(area).copy(stripe), wholeLine.copy(stripe));

            const auto band = LineRenderer::backgroundBand(area.font(), vp.lineHeight);
            const QFontMetrics fm(area.font());
            const QRect ink = fm.tightBoundingRect(QStringLiteral("L"));
            const int baseline = band.baseline;
            const int above = baseline + ink.top() - band.offset;
            const int below = band.offset + band.height - (baseline + ink.bottom() + 1);
            QCOMPARE(above, below);
            QCOMPARE(band.offset, 0);
            QVERIFY(band.height >= vp.lineHeight - 1);
            // Actual capital glyphs are centered in their own row, not at its top.
            int firstInk = wholeLine.height(), lastInk = -1;
            for (int y = vp.lineHeight; y < 2 * vp.lineHeight; ++y)
                for (int x = 4; x < 4 + 3 * vp.charWidth; ++x) {
                    const QColor color = wholeLine.pixelColor(x, y);
                    if (color.red() < 100 && color.green() < 100 && color.blue() < 100) {
                        firstInk = qMin(firstInk, y);
                        lastInk = qMax(lastInk, y);
                    }
                }
            QVERIFY(lastInk >= firstInk);
            QCOMPARE(firstInk - vp.lineHeight, above);
            QCOMPARE(vp.lineHeight + band.height - lastInk - 1, below);
        }
    }

    void bulkRangesAndInvalidColorPriority() {
        SimpleTextDocument doc;
        doc.setText(QString(50000, 'x'));
        CountingHighlighter syntax;
        CodeEditArea area;
        area.setDocument(&doc);
        area.setHighlighter(&syntax);
        show(area);
        const int calls = syntax.calls;
        QVector<ExtraSelection> ranges;
        for (int i = 0; i < 20000; ++i)
            ranges.append({{0, i * 2}, {0, i * 2 + 1}, Qt::yellow, {}});
        // Last entry with invalid colors restores the syntax style here.
        ranges.append(ExtraSelection{{0, 0}, {0, 1}, {}, {}});
        area.setExtraSelections(ranges);
        const auto image = render(area);
        const auto vp = area.viewportState();
        QCOMPARE(cell(image, vp, 0), QColor(Qt::cyan));
        QCOMPARE(cell(image, vp, 2), QColor(Qt::yellow));
        QCOMPARE(cell(image, vp, 3), QColor(Qt::cyan));
        QCOMPARE(area.extraSelections().size(), 20001);
        QCOMPARE(syntax.calls, calls);
        area.setExtraSelections({});
        QCOMPARE(cell(render(area), vp, 2), QColor(Qt::cyan));
        QCOMPARE(syntax.calls, calls);
    }

    void normalizeClearEditsAndReplacement() {
        SimpleTextDocument doc;
        doc.setText("abc\ndef");
        CodeEditArea area;
        area.setDocument(&doc);
        area.setExtraSelections({{{99, 99}, {-1, -1}, Qt::yellow, {}},
                                 {{0, 2}, {0, 2}, Qt::red, {}}});
        QCOMPARE(area.extraSelections().size(), 1);
        QCOMPARE(area.extraSelections()[0].start, (TextCursor{0, 0}));
        QCOMPARE(area.extraSelections()[0].end, (TextCursor{1, 3}));
        doc.removeText({0, 0}, {1, 3});
        QVERIFY(area.extraSelections().isEmpty());
        doc.setText("abc");
        area.setExtraSelections({{{0, 0}, {0, 3}, Qt::yellow, {}}});
        doc.insertText({0, 0}, "x");
        QVERIFY(area.extraSelections().isEmpty());
        area.setExtraSelections({{{0, 0}, {0, 3}, Qt::yellow, {}}});
        doc.insertText({0, 0}, "x\ny");
        QVERIFY(area.extraSelections().isEmpty());
        area.setExtraSelections({{{0, 0}, {0, 3}, Qt::yellow, {}}});
        doc.setText("reset");
        QVERIFY(area.extraSelections().isEmpty());
        area.setExtraSelections({{{0, 0}, {0, 3}, Qt::yellow, {}}});
        SimpleTextDocument replacement;
        area.setDocument(&replacement);
        QVERIFY(area.extraSelections().isEmpty());
    }
};
QTEST_MAIN(TestExtraSelections)
#include "test_extra_selections.moc"
