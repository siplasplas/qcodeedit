#include <QtTest>
#include <QScrollBar>
#include <QMouseEvent>

#include <qce/CodeEditArea.h>
#include <qce/IFoldingProvider.h>
#include <qce/ITextDocument.h>
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>
#include <qce/Rail.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/margins/LineNumberGutter.h>

using namespace qce;

// One "curly" region per matching {…} pair spanning lines.
class BraceFoldingProvider : public IFoldingProvider {
public:
    QVector<FoldRegion> computeRegions(const ITextDocument* doc) const override {
        QVector<FoldRegion> regions;
        QVector<QPair<int, int>> open;
        for (int line = 0; line < doc->lineCount(); ++line) {
            const QString text = doc->lineAt(line);
            for (int col = 0; col < text.size(); ++col) {
                if (text.at(col) == QLatin1Char('{')) {
                    open.append({line, col});
                } else if (text.at(col) == QLatin1Char('}') && !open.isEmpty()) {
                    const auto o = open.takeLast();
                    FoldRegion r;
                    r.startLine = o.first;
                    r.startColumn = o.second;
                    r.endLine = line;
                    r.endColumn = col + 1;
                    r.group = QStringLiteral("curly");
                    regions.append(r);
                }
            }
        }
        return regions;
    }
};

class TestCodeEditAreaKeys : public QObject {
    Q_OBJECT

private:
    // "a", then a function whose body (lines 2-4) folds, then "b".
    static void setUpFolded(SimpleTextDocument& doc, CodeEditArea& area,
                            BraceFoldingProvider& provider) {
        doc.setText(QStringLiteral("a\nf() {\n  x\n  y\n}\nb"));
        area.resize(300, 200);
        area.setDocument(&doc);
        area.setWordWrap(true);
        area.setFoldingProvider(&provider);
        area.toggleFoldAt(1);
        QVERIFY(area.foldState().isCollapsed(area.foldState().regionStartingAt(1)));
    }

    static bool collapsedAt(const CodeEditArea& area, int line) {
        const int idx = area.foldState().regionStartingAt(line);
        return idx >= 0 && area.foldState().isCollapsed(idx);
    }
    // Three lines of known lengths: "Hello" (5), "World!" (6), "." (1)
    static SimpleTextDocument* makeDoc(QObject* parent = nullptr) {
        auto* doc = new SimpleTextDocument(parent);
        doc->setText(QStringLiteral("Hello\nWorld!\n."));
        return doc;
    }

    // Show widget and give it focus so keyPressEvent is delivered.
    static void activate(QWidget* w) {
        w->show();
        w->setFocus();
        QVERIFY(QTest::qWaitForWindowExposed(w));
    }

private slots:
    void editing_keepsCollapsedRegion() {
        SimpleTextDocument doc;
        CodeEditArea area;
        BraceFoldingProvider provider;
        setUpFolded(doc, area, provider);
        activate(&area);

        // Typing on another line.
        area.setCursorPosition({0, 1});
        QTest::keyClicks(&area, QStringLiteral("zz"));
        QCOMPARE(doc.lineAt(0), QStringLiteral("azz"));
        QVERIFY(collapsedAt(area, 1));

        // A new line above moves the collapsed region down with its text.
        QTest::keyClick(&area, Qt::Key_Return);
        QVERIFY(collapsedAt(area, 2));
        QVERIFY(!collapsedAt(area, 1));

        // Undo moves it back.
        area.undo();
        QVERIFY(collapsedAt(area, 1));

        // Removing a line above moves it up.
        area.redo();
        QVERIFY(collapsedAt(area, 2));
        area.setCursorPosition({1, 0});
        QTest::keyClick(&area, Qt::Key_Backspace);
        QCOMPARE(doc.lineAt(1), QStringLiteral("f() {"));
        QVERIFY(collapsedAt(area, 1));

        // Typing below the region.
        area.setCursorPosition({5, 1});
        QTest::keyClicks(&area, QStringLiteral("q"));
        QVERIFY(collapsedAt(area, 1));
    }

    void enterAtEndOfCollapsedHeader_expandsRegion() {
        SimpleTextDocument doc;
        CodeEditArea area;
        BraceFoldingProvider provider;
        setUpFolded(doc, area, provider);
        activate(&area);

        area.setCursorPosition({1, 5});   // after "{"
        QTest::keyClick(&area, Qt::Key_Return);
        QCOMPARE(area.cursorPosition().line, 2);
        QVERIFY(area.foldState().isLineVisible(2));
        QVERIFY(!collapsedAt(area, 1));
    }

    void placeholderClick_doesNotStartDragSelection() {
        for (int cursorLine : {0, 3}) {
            SimpleTextDocument doc;
            doc.setText(QStringLiteral("before\nheader\nbody\nafter"));
            CodeEditArea area;
            area.resize(300, 200);
            area.setDocument(&doc);
            area.setWordWrap(true);
            activate(&area);
            FoldRegion fold;
            fold.startLine = 1;
            fold.endLine = 2;
            fold.placeholder = QStringLiteral("...");
            area.foldState().setRegions({fold});
            area.toggleFoldAt(1);
            area.setCursorPosition({cursorLine, 0});
            const int lh = area.viewportState().lineHeight;
            const QPoint placeholder(7, lh + lh / 2);
            const QPoint destination(20, (cursorLine == 0 ? 3 * lh : 0) + lh / 2);
            auto dragTo = [&](QPoint pos) {
                QMouseEvent move(QEvent::MouseMove, QPointF(pos),
                                 QPointF(area.viewport()->mapToGlobal(pos)),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(area.viewport(), &move);
            };

            QTest::mousePress(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                              placeholder);
            QVERIFY(!area.foldState().isCollapsed(0));
            dragTo(destination);
            QTest::mouseRelease(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                                destination);
            QVERIFY(!area.hasSelection());
            QCOMPARE(area.cursorPosition(), (TextCursor{cursorLine, 0}));

            // A subsequent drag beginning in text still selects normally.
            QTest::mousePress(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                              QPoint(4, lh / 2));
            dragTo(QPoint(20, 3 * lh + lh / 2));
            QVERIFY(area.hasSelection());
            QTest::mouseRelease(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                                QPoint(20, 3 * lh + lh / 2));
            const TextCursor selectionEnd = area.selectionEnd();
            dragTo(QPoint(30, 2 * lh));
            QCOMPARE(area.selectionEnd(), selectionEnd);
        }
    }

    void foldingChevronHover_isLimitedToFoldingStrip() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("header\nbody\nfolded\nbody"));
        CodeEditArea area;
        area.resize(240, 180);
        area.setDocument(&doc);
        area.setWordWrap(true);
        FoldRegion expanded;
        expanded.startLine = 0;
        expanded.endLine = 1;
        FoldRegion collapsed;
        collapsed.startLine = 2;
        collapsed.endLine = 3;
        area.foldState().setRegions({expanded, collapsed});

        LineNumberGutter numbers(&doc);
        numbers.setFont(area.font());
        FoldingGutter folding(&area.foldState(), {});
        Rail rail;
        rail.addMargin(&numbers);
        rail.addMargin(&folding);
        rail.connectToArea(&area);
        area.toggleFoldAt(2);
        const auto vp = area.viewportState();
        rail.resize(rail.sizeHint().width(), vp.viewportHeight);
        const int stripX = numbers.preferredWidth(vp);
        const int stripWidth = folding.preferredWidth(vp);
        auto renderStrip = [&]() {
            QImage image(rail.size(), QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            rail.render(&image);
            return image.copy(stripX, 0, stripWidth, vp.lineHeight * 3);
        };
        auto movePointer = [&](QPoint pos) {
            QMouseEvent event(QEvent::MouseMove, QPointF(pos),
                              QPointF(rail.mapToGlobal(pos)), Qt::NoButton,
                              Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&rail, &event);
        };
        const auto idle = renderStrip();
        const auto expandedRow = idle.copy(0, 0, stripWidth, vp.lineHeight);
        const auto collapsedRow = idle.copy(0, 2 * vp.lineHeight,
                                            stripWidth, vp.lineHeight);
        QVERIFY(expandedRow != collapsedRow);

        movePointer(QPoint(stripX + stripWidth / 2, vp.lineHeight));
        const auto hovered = renderStrip();
        QVERIFY(hovered.copy(0, 0, stripWidth, vp.lineHeight) != expandedRow);
        QCOMPARE(hovered.copy(0, 2 * vp.lineHeight, stripWidth, vp.lineHeight),
                 collapsedRow);

        movePointer(QPoint(stripX / 2, vp.lineHeight));
        QCOMPARE(renderStrip(), idle);
        movePointer(QPoint(stripX + stripWidth / 2, vp.lineHeight));
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(&rail, &leave);
        QCOMPARE(renderStrip(), idle);
    }

    void wrappedRows_foldScrollAndClickMapToDocument() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("header\nhidden\nhidden\n")
                    + QString(80, QLatin1Char('x'))
                    + QStringLiteral("\ntail").repeated(30));
        CodeEditArea area;
        area.resize(240, 180);
        area.setDocument(&doc);
        area.setWordWrap(true);
        activate(&area);

        FoldRegion fold;
        fold.startLine = 0;
        fold.endLine = 2;
        fold.placeholder = QStringLiteral("...");
        area.foldState().setRegions({fold});
        area.toggleFoldAt(0);

        const auto vp = area.viewportState();
        QVERIFY(vp.rows.size() >= 3);
        QCOMPARE(vp.firstVisibleLine, 0);
        QCOMPARE(vp.rows[0].foldPlaceholder, fold.placeholder);
        QCOMPARE(vp.rows[1].logicalLine, 3);
        QCOMPARE(vp.rows[2].logicalLine, 3);
        QVERIFY(vp.rows[2].startCol > 0);
        QCOMPARE(vp.lastVisibleLine, vp.rows.last().logicalLine);

        QTest::mouseClick(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(0, 2 * vp.lineHeight + vp.lineHeight / 2));
        QCOMPARE(area.cursorPosition(), (TextCursor{3, vp.rows[2].startCol}));

        const int foldedMaximum = area.verticalScrollBar()->maximum();
        QVERIFY(foldedMaximum > 0);
        area.verticalScrollBar()->setValue(foldedMaximum);
        const auto scrolled = area.viewportState();
        QCOMPARE(scrolled.firstVisibleRow, foldedMaximum);
        QCOMPARE(scrolled.firstVisibleLine, scrolled.rows.first().logicalLine);
        QCOMPARE(scrolled.lastVisibleLine, doc.lineCount() - 1);
        QTest::mouseClick(area.viewport(), Qt::LeftButton, Qt::NoModifier,
                          QPoint(0, scrolled.lineHeight / 2));
        QCOMPARE(area.cursorPosition(),
                 (TextCursor{scrolled.rows.first().logicalLine,
                             scrolled.rows.first().startCol}));

        area.unfoldAll();
        QCOMPARE(area.verticalScrollBar()->maximum(), foldedMaximum + 2);
        area.verticalScrollBar()->setValue(0);
        QCOMPARE(area.viewportState().rows[1].logicalLine, 1);
        QCOMPARE(area.viewportState().rows[2].logicalLine, 2);
    }

    void rightArrow_movesColumnRight() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Right);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 1}));
    }

    void leftArrow_clampsAtLineStart() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Left);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 0}));
    }

    void downArrow_movesLineDown() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello\nWorld!"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Down);
        QCOMPARE(area.cursorPosition().line, 1);
    }

    void upArrow_clampsAtFirstLine() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello\nWorld!"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Up);
        QCOMPARE(area.cursorPosition().line, 0);
    }

    void endKey_movesToLineEnd() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 5}));
    }

    void homeKey_movesToLineStart() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End);   // go to end first
        QTest::keyClick(&area, Qt::Key_Home);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 0}));
    }

    void ctrlEnd_movesToLastLine() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello\nWorld!\n."));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End, Qt::ControlModifier);
        QCOMPARE(area.cursorPosition().line, 2);
    }

    void ctrlHome_movesToFirstLine() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello\nWorld!\n."));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End,  Qt::ControlModifier);
        QTest::keyClick(&area, Qt::Key_Home, Qt::ControlModifier);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 0}));
    }

    void rightArrow_wrapsToNextLine() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hi\nThere"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End);
        QTest::keyClick(&area, Qt::Key_Right);
        QCOMPARE(area.cursorPosition(), (TextCursor{1, 0}));
    }

    void leftArrow_wrapsToEndOfPreviousLine() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hi\nThere"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Down);
        QTest::keyClick(&area, Qt::Key_Left);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 2}));
    }

    void ctrlArrows_stopAtWordsAndPunctuation() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("obj.method(x)"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        const int expected[] = {3, 4, 10, 11, 12, 13};
        for (int col : expected) {
            QTest::keyClick(&area, Qt::Key_Right, Qt::ControlModifier);
            QCOMPARE(area.cursorPosition(), (TextCursor{0, col}));
        }
        QTest::keyClick(&area, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 12}));
        QTest::keyClick(&area, Qt::Key_Left, Qt::ControlModifier);
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 11}));
    }

    void ctrlBackspace_deletesPreviousWord() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("one two_3  \nnext"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_End);
        QTest::keyClick(&area, Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(doc.lineAt(0), QStringLiteral("one "));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 4}));

        // At column 0 it joins with the previous line, like Backspace.
        QTest::keyClick(&area, Qt::Key_Down);
        QTest::keyClick(&area, Qt::Key_Home);
        QTest::keyClick(&area, Qt::Key_Backspace, Qt::ControlModifier);
        QCOMPARE(doc.lineAt(0), QStringLiteral("one next"));
    }

    void ctrlDelete_deletesNextWord() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("alpha, beta\ngamma"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        QTest::keyClick(&area, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(doc.lineAt(0), QStringLiteral(", beta"));
        QTest::keyClick(&area, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(doc.lineAt(0), QStringLiteral("beta"));

        // At the line end it joins with the next line, like Delete.
        QTest::keyClick(&area, Qt::Key_End);
        QTest::keyClick(&area, Qt::Key_Delete, Qt::ControlModifier);
        QCOMPARE(doc.lineAt(0), QStringLiteral("betagamma"));
    }
};

QTEST_MAIN(TestCodeEditAreaKeys)
#include "test_code_edit_area_keys.moc"
