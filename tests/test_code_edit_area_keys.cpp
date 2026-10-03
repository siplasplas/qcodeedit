#include <QtTest>
#include <QScrollBar>
#include <QMouseEvent>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>
#include <qce/Rail.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/margins/LineNumberGutter.h>

using namespace qce;

class TestCodeEditAreaKeys : public QObject {
    Q_OBJECT

private:
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
};

QTEST_MAIN(TestCodeEditAreaKeys)
#include "test_code_edit_area_keys.moc"
