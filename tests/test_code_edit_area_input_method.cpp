#include <QtTest>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCharFormat>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/TextCursor.h>

using namespace qce;

// Input-method support of CodeEditArea. Events are sent directly with
// QCoreApplication::sendEvent(), so no real input method is needed.
class TestCodeEditAreaInputMethod : public QObject {
    Q_OBJECT

private:
    static void activate(CodeEditArea* w) {
        w->setCaretBlinkInterval(1000000);  // keep the caret steady for grabs
        w->show();
        w->setFocus();
        QVERIFY(QTest::qWaitForWindowExposed(w));
    }

    static void sendIm(QWidget* w, const QString& preedit, const QString& commit,
                       const QList<QInputMethodEvent::Attribute>& attrs = {},
                       int replaceFrom = 0, int replaceLength = 0) {
        QInputMethodEvent ev(preedit, attrs);
        ev.setCommitString(commit, replaceFrom, replaceLength);
        QCoreApplication::sendEvent(w, &ev);
    }

    static void sendKey(QWidget* w, int key, Qt::KeyboardModifiers mods, const QString& text) {
        QKeyEvent press(QEvent::KeyPress, key, mods, text);
        QCoreApplication::sendEvent(w, &press);
    }

    static QInputMethodEvent::Attribute underline(int start, int length) {
        QTextCharFormat f;
        f.setFontUnderline(true);
        return {QInputMethodEvent::TextFormat, start, length, f};
    }

    static QRect cursorRect(const CodeEditArea& a) {
        return a.inputMethodQuery(Qt::ImCursorRectangle).toRect();
    }

private slots:
    // 1
    void commit_insertsAtCursor_undoRedo() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("ab"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});

        sendIm(&area, {}, QStringLiteral("ą"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("aąb"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 2}));
        area.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("ab"));
        area.redo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("aąb"));
    }

    // 2
    void commit_replacesSelection_oneUndoStep() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Hello world"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setSelection({0, 0}, {0, 5});

        sendIm(&area, {}, QStringLiteral("Bye"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("Bye world"));
        area.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("Hello world"));
    }

    // 3
    void preedit_leavesDocument_commitReplacesIt() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("x"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});

        sendIm(&area, QStringLiteral("ni"), {}, {underline(0, 2)});
        QCOMPARE(area.preeditString(), QStringLiteral("ni"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("x"));
        QCOMPARE(area.inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("x"));
        QVERIFY(!area.canUndo());

        sendIm(&area, {}, QStringLiteral("你"));
        QVERIFY(area.preeditString().isEmpty());
        QCOMPARE(doc.toPlainText(), QStringLiteral("x你"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 2}));
    }

    // 4
    void emptyEvent_cancelsComposition() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.resize(300, 120);
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});
        const QImage before = area.viewport()->grab().toImage();

        sendIm(&area, QStringLiteral("ni"), {}, {underline(0, 2)});
        QVERIFY(area.viewport()->grab().toImage() != before);  // pre-edit painted
        sendIm(&area, {}, {});
        QVERIFY(area.preeditString().isEmpty());
        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
        QVERIFY(!area.canUndo());
        QCOMPARE(area.viewport()->grab().toImage(), before);
    }

    // 5
    void replacementBeforeCursor_oneUndoStep() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("a"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});

        sendIm(&area, {}, QStringLiteral("ą"), {}, -1, 1);
        QCOMPARE(doc.toPlainText(), QStringLiteral("ą"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 1}));
        area.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("a"));
        QVERIFY(!area.canUndo());
    }

    // 6
    void multiCharCommit_movesByQCharLength() {
        SimpleTextDocument doc;
        doc.setText(QString());
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        sendIm(&area, {}, QStringLiteral("日本語"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 3}));
        sendIm(&area, {}, QStringLiteral("😀"));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 5}));
        QCOMPARE(doc.toPlainText(), QStringLiteral("日本語😀"));

        area.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("日本語"));
        area.undo();
        QCOMPARE(doc.toPlainText(), QString());
    }

    // 7
    void readOnly_ignoresEvents_andDisablesInputMethod() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        QVERIFY(area.testAttribute(Qt::WA_InputMethodEnabled));
        QVERIFY(area.inputMethodQuery(Qt::ImEnabled).toBool());

        area.setReadOnly(true);
        QVERIFY(!area.testAttribute(Qt::WA_InputMethodEnabled));
        QVERIFY(!area.inputMethodQuery(Qt::ImEnabled).toBool());
        sendIm(&area, QStringLiteral("ni"), QStringLiteral("x"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
        QVERIFY(area.preeditString().isEmpty());

        area.setReadOnly(false);
        QVERIFY(area.testAttribute(Qt::WA_InputMethodEnabled));
        QVERIFY(area.inputMethodQuery(Qt::ImEnabled).toBool());
    }

    // 8
    void overwriteMode_commitReplacesCharUnderCursor() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        QTest::keyClick(&area, Qt::Key_Insert);
        QVERIFY(area.overwriteMode());

        sendIm(&area, {}, QStringLiteral("ą"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("ąbc"));
        area.undo();
        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
        QVERIFY(!area.canUndo());
    }

    // 9
    void queries_reportLineCursorAndRectangle() {
        SimpleTextDocument doc;
        QStringList lines{QStringLiteral("Hello world")};
        for (int i = 0; i < 100; ++i) lines << QStringLiteral("line %1").arg(i);
        doc.setText(lines.join(QLatin1Char('\n')));
        CodeEditArea area;
        area.resize(300, 200);
        area.setDocument(&doc);
        activate(&area);

        area.setSelection({0, 6}, {0, 11});
        QCOMPARE(area.inputMethodQuery(Qt::ImCursorPosition).toInt(), 11);
        QCOMPARE(area.inputMethodQuery(Qt::ImAnchorPosition).toInt(), 6);
        QCOMPARE(area.inputMethodQuery(Qt::ImSurroundingText).toString(), QStringLiteral("Hello world"));
        QCOMPARE(area.inputMethodQuery(Qt::ImCurrentSelection).toString(), QStringLiteral("world"));

        area.setCursorPosition({0, 5});
        QCOMPARE(area.inputMethodQuery(Qt::ImCurrentSelection).toString(), QString());
        QCOMPARE(area.inputMethodQuery(Qt::ImTextBeforeCursor).toString(), QStringLiteral("Hello"));
        QCOMPARE(area.inputMethodQuery(Qt::ImTextAfterCursor).toString(), QStringLiteral(" world"));
        QVERIFY(area.inputMethodQuery(Qt::ImHints).toInt() & Qt::ImhMultiLine);
        QCOMPARE(area.inputMethodQuery(Qt::ImFont).value<QFont>(), area.font());

        const QRect r0 = cursorRect(area);
        QVERIFY(area.viewport()->geometry().contains(r0.topLeft()));
        sendIm(&area, {}, QStringLiteral("x"));
        QVERIFY(cursorRect(area).x() > r0.x());

        area.setCursorPosition({10, 0});
        const QRect r1 = cursorRect(area);
        area.verticalScrollBar()->setValue(area.verticalScrollBar()->value() + 1);
        QCOMPARE(cursorRect(area).y(), r1.y() - area.viewportState().lineHeight);
    }

    // 10
    void wordWrap_rectangleOnSecondVisualRow() {
        SimpleTextDocument doc;
        doc.setText(QString(80, QLatin1Char('x')));
        CodeEditArea area;
        area.resize(240, 180);
        area.setDocument(&doc);
        area.setWordWrap(true);
        activate(&area);

        const auto vp = area.viewportState();
        QVERIFY(vp.rows.size() >= 2);
        area.setCursorPosition({0, vp.rows[1].startCol + 2});
        const QRect r = cursorRect(area);
        QCOMPARE(r.y(), area.viewport()->y() + vp.lineHeight);
    }

    // 11
    void preeditCursorAttribute_movesRectangle() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});
        const QRect base = cursorRect(area);

        sendIm(&area, QStringLiteral("wxyz"), {},
               {underline(0, 4), {QInputMethodEvent::Cursor, 2, 1, QVariant()}});
        const QFontMetrics fm(area.font());
        QCOMPARE(cursorRect(area).x(), base.x() + fm.horizontalAdvance(QStringLiteral("wx")));

        // Without a Cursor attribute the caret sits after the pre-edit.
        sendIm(&area, QStringLiteral("wxyz"), {}, {underline(0, 4)});
        QCOMPARE(cursorRect(area).x(), base.x() + fm.horizontalAdvance(QStringLiteral("wxyz")));
    }

    // 12
    void altGr_insertsText_shortcutsStillWork() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("{\n}"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});

        // Windows: AltGr+A arrives as Ctrl+Alt+A; must not select all.
        sendKey(&area, Qt::Key_A, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("ą"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("{ą\n}"));
        QVERIFY(!area.hasSelection());

        sendKey(&area, Qt::Key_Z, Qt::GroupSwitchModifier, QStringLiteral("ż"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("{ąż\n}"));

        sendKey(&area, Qt::Key_Z, Qt::ControlModifier, QStringLiteral("\x1a"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("{\n}"));  // typing merged into one step

        // Set after the edits: without a folding provider every edit clears regions.
        FoldRegion fold;
        fold.startLine = 0;
        fold.endLine = 1;
        area.foldState().setRegions({fold});
        area.setCursorPosition({0, 0});
        sendKey(&area, Qt::Key_Plus, Qt::ControlModifier, QStringLiteral("+"));
        QVERIFY(area.foldState().isCollapsed(0));
    }

    // 13
    void combiningMarkKeyText_isInserted() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("e"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);
        area.setCursorPosition({0, 1});

        sendKey(&area, 0, Qt::NoModifier, QStringLiteral("́"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("é"));

        sendKey(&area, 0, Qt::NoModifier, QStringLiteral("😀"));
        QCOMPARE(doc.toPlainText(), QStringLiteral("é😀"));
    }

    // 14
    void focusOutDocumentAndCursorMove_clearPreedit() {
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("abc"));
        CodeEditArea area;
        area.setDocument(&doc);
        activate(&area);

        sendIm(&area, QStringLiteral("ni"), {});
        QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
        QCoreApplication::sendEvent(&area, &out);
        QVERIFY(area.preeditString().isEmpty());
        area.setFocus();

        sendIm(&area, QStringLiteral("ni"), {});
        area.setCursorPosition({0, 2});
        QVERIFY(area.preeditString().isEmpty());

        sendIm(&area, QStringLiteral("ni"), {});
        SimpleTextDocument other;
        other.setText(QStringLiteral("other"));
        area.setDocument(&other);
        QVERIFY(area.preeditString().isEmpty());

        QCOMPARE(doc.toPlainText(), QStringLiteral("abc"));
        QCOMPARE(other.toPlainText(), QStringLiteral("other"));
    }
};

QTEST_MAIN(TestCodeEditAreaInputMethod)
#include "test_code_edit_area_input_method.moc"
