#include <QtTest>

#include <qce/margins/LineNumberGutter.h>
#include <qce/SimpleTextDocument.h>
#include <qce/ViewportState.h>

#include <memory>

using namespace qce;

class TestLineNumberGutter : public QObject {
    Q_OBJECT

private:
    static ViewportState makeVp(int charWidth = 10) {
        ViewportState vp;
        vp.charWidth      = charWidth;
        vp.lineHeight     = 16;
        vp.viewportWidth  = 800;
        vp.viewportHeight = 600;
        vp.firstVisibleLine = 0;
        vp.lastVisibleLine  = 0;
        return vp;
    }

    static std::unique_ptr<SimpleTextDocument> makeDoc(int n) {
        auto doc = std::make_unique<SimpleTextDocument>();
        QStringList lines;
        lines.reserve(n);
        for (int i = 0; i < n; ++i) {
            lines << QStringLiteral("x");
        }
        doc->setText(lines.join(QLatin1Char('\n')));
        return doc;
    }

private slots:
    void preferredWidth_noDocument() {
        // No document → 0 lines → 1 digit minimum
        LineNumberGutter g;
        const ViewportState vp = makeVp(10);
        QCOMPARE(g.preferredWidth(vp), 1 * 10 + 2 * 4);
    }

    void preferredWidth_oneToNineLines() {
        // 1..9 lines → 1 digit
        for (int n = 1; n <= 9; ++n) {
            auto doc = makeDoc(n);
            LineNumberGutter g(doc.get());
            const ViewportState vp = makeVp(10);
            QCOMPARE(g.preferredWidth(vp), 1 * 10 + 2 * 4);
        }
    }

    void preferredWidth_tenLines() {
        auto doc = makeDoc(10);
        LineNumberGutter g(doc.get());
        const ViewportState vp = makeVp(8);
        QCOMPARE(g.preferredWidth(vp), 2 * 8 + 2 * 4);
    }

    void preferredWidth_ninetyNineLines() {
        auto doc = makeDoc(99);
        LineNumberGutter g(doc.get());
        const ViewportState vp = makeVp(8);
        QCOMPARE(g.preferredWidth(vp), 2 * 8 + 2 * 4);
    }

    void preferredWidth_hundredLines() {
        auto doc = makeDoc(100);
        LineNumberGutter g(doc.get());
        const ViewportState vp = makeVp(8);
        QCOMPARE(g.preferredWidth(vp), 3 * 8 + 2 * 4);
    }

    void preferredWidth_thousandLines() {
        auto doc = makeDoc(1000);
        LineNumberGutter g(doc.get());
        const ViewportState vp = makeVp(8);
        QCOMPARE(g.preferredWidth(vp), 4 * 8 + 2 * 4);
    }
};

QTEST_APPLESS_MAIN(TestLineNumberGutter)
#include "test_line_number_gutter.moc"
