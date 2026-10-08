#include <QtTest>

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/ViewportState.h>
#include <qce/margins/LineNumberGutter.h>

using namespace qce;

// Renders a CodeEdit with a line-number gutter and checks the rail colours
// and the wrap arrows on continuation rows.
class TestGutterColors : public QObject {
    Q_OBJECT

private:
    struct Editor {
        SimpleTextDocument doc;
        CodeEdit edit;
        LineNumberGutter numbers;

        explicit Editor(bool wrap) : numbers(&doc) {
            doc.setText(QStringLiteral("short\n"
                                       "a long line that has to wrap over several rows\n"
                                       "end"));
            edit.setDocument(&doc);
            numbers.setFont(edit.area()->font());
            edit.addLeftMargin(&numbers);
            edit.area()->setWordWrap(wrap);
            edit.resize(220, 160);
            edit.show();
            QCoreApplication::processEvents();
        }

        QImage image() { return edit.grab().toImage(); }
        int gutterRight() const { return edit.area()->x() - 1; }
        int rowTop(int row) const {
            const ViewportState vp = edit.area()->viewportState();
            return edit.area()->y() + vp.contentOffsetY + row * vp.lineHeight;
        }
    };

    // Pixels in the gutter (separator excluded) of `row` that differ from
    // the gutter background.
    static int inkInGutterRow(Editor& e, const QImage& img, int row, QRgb background) {
        const int lh = e.edit.area()->viewportState().lineHeight;
        int ink = 0;
        for (int y = e.rowTop(row); y < e.rowTop(row) + lh; ++y)
            for (int x = 0; x < e.gutterRight(); ++x)
                if (img.pixel(x, y) != background) ++ink;
        return ink;
    }

private slots:
    void defaultsAreBlackOnWhiteWithKateGutter() {
        Editor e(false);
        const QImage img = e.image();
        const int y = e.rowTop(0) + 1;
        QCOMPARE(QColor(img.pixel(1, y)), QColor(QStringLiteral("#f0f0f0")));
        QCOMPARE(QColor(img.pixel(e.gutterRight(), y)), QColor(QStringLiteral("#d5d5d5")));
        QCOMPARE(QColor(img.pixel(e.edit.area()->x() + 150, e.rowTop(4))), QColor(Qt::white));
    }

    void explicitColoursWin() {
        Editor e(false);
        e.edit.setGutterColors({QColor("#102030"), QColor("#a0b0c0"), QColor("#405060")});
        QCOMPARE(e.edit.gutterColors().background, QColor("#102030"));
        const QImage img = e.image();
        const int y = e.rowTop(0) + 1;
        QCOMPARE(QColor(img.pixel(1, y)), QColor("#102030"));
        QCOMPARE(QColor(img.pixel(e.gutterRight(), y)), QColor("#405060"));
    }

    void derivedColoursFollowPalette() {
        Editor e(false);
        QPalette pal = e.edit.palette();
        pal.setColor(QPalette::Base, Qt::black);
        pal.setColor(QPalette::Text, Qt::white);
        e.edit.setPalette(pal);
        QCoreApplication::processEvents();
        const QImage img = e.image();
        // 6% of the way from the black background to the white text.
        QCOMPARE(QColor(img.pixel(1, e.rowTop(0) + 1)), QColor(15, 15, 15));
    }

    void continuationRowsGetWrapArrow() {
        Editor e(true);
        const ViewportState vp = e.edit.area()->viewportState();
        int continuation = -1;
        for (int i = 0; i < vp.rows.size() && continuation < 0; ++i)
            if (!vp.rows[i].isFirstRow) continuation = i;
        QVERIFY(continuation > 0);
        const QImage img = e.image();
        const QRgb background = QColor(QStringLiteral("#f0f0f0")).rgb();
        QVERIFY(inkInGutterRow(e, img, continuation, background) > 0);
        // The row below the last line stays empty.
        QCOMPARE(inkInGutterRow(e, img, int(vp.rows.size()), background), 0);
    }

    void noArrowsWithoutWrap() {
        Editor e(false);
        const QImage img = e.image();
        const QRgb background = QColor(QStringLiteral("#f0f0f0")).rgb();
        // Line 3 is the last line; the row below it stays empty.
        QVERIFY(inkInGutterRow(e, img, 2, background) > 0);
        QCOMPARE(inkInGutterRow(e, img, 3, background), 0);
    }
};

QTEST_MAIN(TestGutterColors)
#include "test_gutter_colors.moc"
