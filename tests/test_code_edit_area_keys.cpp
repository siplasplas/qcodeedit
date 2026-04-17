#include <QtTest>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>

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
