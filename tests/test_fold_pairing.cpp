#include <qce/FoldMarker.h>
#include <qce/FoldPairing.h>

#include <qce/HighlightState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/kate/KateXmlReader.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

#include "KateTestPaths.h"

#ifndef QCE_TEST_DATA_DIR
#  error "QCE_TEST_DATA_DIR must be set by CMake"
#endif

using namespace qce;

static FoldMarker mk(int col, int len, int rid, bool begin) {
    return FoldMarker{col, len, rid, begin};
}

class TestFoldPairing : public QObject {
    Q_OBJECT
private slots:
    // §4.2 — pairing locality
    void simplePair_keptWhenInBudget();
    void simplePair_droppedWhenSpanExceedsBudget();
    void nested_depthReflectsStackPosition();
    void unmatchedOpen_dropped();
    void unmatchedEnd_ignored();
    void mostRecentOpenWins_discardsInterveningOpens();
    void emptyInput();
    void budgetZero_dropsEverything();

    // §4.3 — Kate regression on real books.xml
    void booksXml_pairsContainerAndBookElements();
};

/// §4.2 — a begin/end pair with span within budget is emitted.
void TestFoldPairing::simplePair_keptWhenInBudget() {
    QVector<FoldMarker> ms = {
        mk(10, 1, 0, true),    // open at byte 10
        mk(100, 1, 0, false),  // close at byte 100..101
    };
    const auto pairs = pairFolds(ms, /*maxSpanBytes=*/200);
    QCOMPARE(pairs.size(), 1);
    QCOMPARE(pairs[0].beginByte, qsizetype(10));
    QCOMPARE(pairs[0].endByte,   qsizetype(101));
    QCOMPARE(pairs[0].regionId,  0);
    QCOMPARE(pairs[0].depth,     0);
}

/// §4.2 — a pair whose span exceeds the budget is dropped.
void TestFoldPairing::simplePair_droppedWhenSpanExceedsBudget() {
    QVector<FoldMarker> ms = {
        mk(0, 1, 0, true),
        mk(10'000, 1, 0, false),    // span = 10_001
    };
    const auto pairs = pairFolds(ms, /*maxSpanBytes=*/1'000);
    QVERIFY(pairs.isEmpty());
}

/// Nested pairs with identical region id — depths are 0 (outer) and 1 (inner).
void TestFoldPairing::nested_depthReflectsStackPosition() {
    const int rid = 42;
    QVector<FoldMarker> ms = {
        mk(0,  1, rid, true),    // outer open
        mk(10, 1, rid, true),    // inner open
        mk(20, 1, rid, false),   // inner close
        mk(30, 1, rid, false),   // outer close
    };
    const auto pairs = pairFolds(ms, /*maxSpanBytes=*/100);
    QCOMPARE(pairs.size(), 2);
    // Inner closes first — emission order matches closer order.
    QCOMPARE(pairs[0].beginByte, qsizetype(10));
    QCOMPARE(pairs[0].endByte,   qsizetype(21));
    QCOMPARE(pairs[0].depth,     1);
    QCOMPARE(pairs[1].beginByte, qsizetype(0));
    QCOMPARE(pairs[1].endByte,   qsizetype(31));
    QCOMPARE(pairs[1].depth,     0);
}

void TestFoldPairing::unmatchedOpen_dropped() {
    QVector<FoldMarker> ms = { mk(0, 1, 0, true) };
    QVERIFY(pairFolds(ms, 100).isEmpty());
}

void TestFoldPairing::unmatchedEnd_ignored() {
    QVector<FoldMarker> ms = { mk(10, 1, 0, false) };
    QVERIFY(pairFolds(ms, 100).isEmpty());
}

/// Kate semantic: closing rid=1 while rid=2 sits on top pops the rid=2 open
/// as unbalanced and pairs rid=1 only.
void TestFoldPairing::mostRecentOpenWins_discardsInterveningOpens() {
    QVector<FoldMarker> ms = {
        mk(0,  1, 1, true),   // open rid=1
        mk(10, 1, 2, true),   // open rid=2 (unbalanced — will be dropped)
        mk(20, 1, 1, false),  // close rid=1
    };
    const auto pairs = pairFolds(ms, /*maxSpanBytes=*/100);
    QCOMPARE(pairs.size(), 1);
    QCOMPARE(pairs[0].regionId, 1);
    QCOMPARE(pairs[0].beginByte, qsizetype(0));
    QCOMPARE(pairs[0].endByte,   qsizetype(21));
}

void TestFoldPairing::emptyInput() {
    QCOMPARE(pairFolds({}, 100).size(), 0);
}

void TestFoldPairing::budgetZero_dropsEverything() {
    // Span is always > 0 for real pairs, so maxSpanBytes=0 drops all.
    QVector<FoldMarker> ms = {
        mk(0, 1, 0, true),
        mk(1, 1, 0, false),   // span = 2 > 0
    };
    QVERIFY(pairFolds(ms, 0).isEmpty());
}

// --------------------------------------------------------------------------
// §4.3 — real xml.xml + books.xml regression
// --------------------------------------------------------------------------

/// With dynamic rules now working, xml.xml emits proper begin/end pairs for
/// every XML element. Verify that feeding books.xml through foldersInBytes
/// + pairFolds yields, at minimum, a top-level <catalog> fold plus a pair
/// per <book>.
void TestFoldPairing::booksXml_pairsContainerAndBookElements() {
    const QString xmlXml = kateSyntaxPath(QStringLiteral("xml.xml"));
    if (xmlXml.isEmpty()) QSKIP("xml.xml not installed on this system");

    auto hl = KateXmlReader::load(xmlXml);
    QVERIFY(hl != nullptr);

    RuleBasedFoldingProvider prov(hl.get());

    const QString booksPath = QStringLiteral(QCE_TEST_DATA_DIR "/books.xml");
    QFile f(booksPath);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(booksPath));
    const QByteArray buf = f.readAll();
    QVERIFY(buf.size() > 0);

    HighlightState s0 = hl->initialState(), s1;
    QVector<FoldMarker> markers;
    prov.foldersInBytes(buf.constData(), buf.size(), s0, markers, s1);

    // At minimum we must have some markers to pair.
    QVERIFY2(!markers.isEmpty(), "no fold markers emitted from xml.xml over books.xml");

    // Budget comfortably larger than the whole file — nothing should be
    // dropped for span reasons.
    const auto pairs = pairFolds(markers, buf.size() + 1024);

    // books.xml has exactly 12 <book> entries at depth 1 (inside <catalog>).
    // Each <book> should produce exactly one fold pair at depth 1.
    const int elementRid = hl->regionIdForName(QStringLiteral("element"));
    QVERIFY(elementRid >= 0);

    int depth0Count = 0;        // catalog
    int depth1Count = 0;        // book (inside catalog)
    for (const auto& p : pairs) {
        if (p.regionId != elementRid) continue;
        if (p.depth == 0) ++depth0Count;
        else if (p.depth == 1) ++depth1Count;
    }
    QCOMPARE(depth0Count, 1);      // <catalog>
    QCOMPARE(depth1Count, 12);     // 12 <book> entries

    // With a tight budget, the whole-file <catalog> fold drops but nested
    // <book> folds (each a few hundred bytes) survive.
    const auto tightPairs = pairFolds(markers, /*maxSpanBytes=*/800);
    int tightBooks = 0, tightCatalog = 0;
    for (const auto& p : tightPairs) {
        if (p.regionId != elementRid) continue;
        if (p.depth == 0) ++tightCatalog;
        else if (p.depth == 1) ++tightBooks;
    }
    QCOMPARE(tightCatalog, 0);     // whole-catalog span exceeds 800 bytes
    QVERIFY(tightBooks >= 1);       // some books still fit
}

QTEST_GUILESS_MAIN(TestFoldPairing)
#include "test_fold_pairing.moc"
