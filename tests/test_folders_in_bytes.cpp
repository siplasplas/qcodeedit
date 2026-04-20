#include <qce/FoldMarker.h>
#include <qce/HighlightState.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QtTest/QtTest>

using namespace qce;

/// Tiny "C-like" highlighter that opens a region on '{' and closes on '}'.
/// Enough to exercise the fold-event path without pulling Kate XML.
static std::unique_ptr<RulesHighlighter> makeBraceHighlighter() {
    auto hl = std::make_unique<RulesHighlighter>();
    const int attr = hl->addAttribute({QColor("purple")});
    const int curlyId = hl->regionIdForName(QStringLiteral("curly"));

    const int ctxNormal = hl->addContext({"Normal", -1, -1, 0, false, -1, 0, {}});
    hl->contextRef(ctxNormal).rules.push_back(
        {HighlightRule::DetectChar, '{', {}, {}, true, {}, -1, -1,
         attr, -1, 0, false, false,
         /*beginRegionId=*/curlyId, /*endRegionId=*/-1, /*column=*/-1, /*dynamic=*/false});
    hl->contextRef(ctxNormal).rules.push_back(
        {HighlightRule::DetectChar, '}', {}, {}, true, {}, -1, -1,
         attr, -1, 0, false, false,
         /*beginRegionId=*/-1, /*endRegionId=*/curlyId, /*column=*/-1, /*dynamic=*/false});
    return hl;
}

static bool markersEqual(const QVector<FoldMarker>& a, const QVector<FoldMarker>& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].column != b[i].column
            || a[i].length != b[i].length
            || a[i].regionId != b[i].regionId
            || a[i].isBegin != b[i].isBegin) return false;
    }
    return true;
}

class TestFoldersInBytes : public QObject {
    Q_OBJECT
private slots:
    void defaultImpl_emitsNothing();
    void ruleBased_ascii_matchesPerLine();            // §4.1 (ASCII subset)
    void ruleBased_utf8_columnsAreByteOffsets();      // §4.1 (UTF-8)
    void stitch_twoChunksThreadedEqualsOneShot();      // §4.4
    void emptyBuffer_producesEmptyMarkers();
    void trailingNewline_matchesOneShot();
};

void TestFoldersInBytes::defaultImpl_emitsNothing() {
    // A provider that inherits the base default must produce nothing.
    class StubProvider : public IFoldingProvider {
    public:
        QVector<FoldRegion> computeRegions(const ITextDocument*) const override { return {}; }
    } p;

    HighlightState s0, s1;
    QVector<FoldMarker> markers;
    p.foldersInBytes("{}{}{}", 6, s0, markers, s1);
    QVERIFY(markers.isEmpty());
    QCOMPARE(s1, s0);
}

/// §4.1 (ASCII). For a pure-ASCII buffer, byte markers from foldersInBytes
/// map to the same line+column positions as per-line highlightLineEx.
void TestFoldersInBytes::ruleBased_ascii_matchesPerLine() {
    auto hl = makeBraceHighlighter();
    RuleBasedFoldingProvider prov(hl.get());

    const QByteArray buf = QByteArrayLiteral("int f() {\n  { x; }\n}\n");

    // Byte-range pass.
    HighlightState s0 = hl->initialState(), s1;
    QVector<FoldMarker> byteMarkers;
    prov.foldersInBytes(buf.constData(), buf.size(), s0, byteMarkers, s1);

    // Per-line reference (matching what the default would do).
    QVector<FoldMarker> refMarkers;
    HighlightState s = hl->initialState();
    qsizetype base = 0;
    const QList<QByteArray> lines = buf.split('\n');
    for (int li = 0; li < lines.size(); ++li) {
        const QByteArray& ln = lines[li];
        QVector<StyleSpan> spans;
        QVector<FoldMarker> folds;
        HighlightState sNext;
        hl->highlightLineEx(QString::fromUtf8(ln), s, spans, sNext, folds);
        for (const auto& f : folds) {
            refMarkers.append(FoldMarker{
                static_cast<int>(base) + f.column,
                f.length, f.regionId, f.isBegin});
        }
        s = sNext;
        base += ln.size() + (li + 1 < lines.size() ? 1 : 0);
    }

    QVERIFY(markersEqual(byteMarkers, refMarkers));
    QCOMPARE(byteMarkers.size(), 4);  // two opens, two closes
}

/// §4.1 (UTF-8). With multi-byte characters before the braces, the
/// byte-offset columns are NOT the QChar columns — verify the remapping.
void TestFoldersInBytes::ruleBased_utf8_columnsAreByteOffsets() {
    auto hl = makeBraceHighlighter();
    RuleBasedFoldingProvider prov(hl.get());

    // "€" is 3 UTF-8 bytes. QChar column of '{' is 2; byte offset is 4.
    const QByteArray buf = QString::fromUtf8("a€{}").toUtf8();
    QCOMPARE(buf.size(), 6);

    HighlightState s0 = hl->initialState(), s1;
    QVector<FoldMarker> markers;
    prov.foldersInBytes(buf.constData(), buf.size(), s0, markers, s1);

    QCOMPARE(markers.size(), 2);
    QCOMPARE(markers[0].column, 4);  // '{' — byte offset past "a" + "€"
    QCOMPARE(markers[0].length, 1);
    QVERIFY(markers[0].isBegin);
    QCOMPARE(markers[1].column, 5);  // '}'
    QCOMPARE(markers[1].length, 1);
    QVERIFY(!markers[1].isBegin);
}

/// §4.4 Stitch property. Tokenise [0, N) and [N, M) with state-threading;
/// the concatenated marker stream (offsets re-based) equals one-shot.
void TestFoldersInBytes::stitch_twoChunksThreadedEqualsOneShot() {
    auto hl = makeBraceHighlighter();
    RuleBasedFoldingProvider prov(hl.get());

    const QByteArray buf =
        QByteArrayLiteral("{\n  {\n    x\n  }\n}\n");
    // Split only at '\n' boundaries — the default line-splitter resumes
    // at line granularity (see byte-range highlighter MVP).
    QVector<qsizetype> splits;
    for (qsizetype i = 0; i < buf.size(); ++i) {
        if (buf[i] == '\n') splits.append(i + 1);
    }

    HighlightState sFull = hl->initialState(), sFullOut;
    QVector<FoldMarker> fullMarkers;
    prov.foldersInBytes(buf.constData(), buf.size(), sFull, fullMarkers, sFullOut);

    for (qsizetype split : splits) {
        HighlightState s0 = hl->initialState(), s1, s2;
        QVector<FoldMarker> first, second;
        prov.foldersInBytes(buf.constData(), split, s0, first, s1);
        prov.foldersInBytes(buf.constData() + split,
                            buf.size() - split, s1, second, s2);

        QVector<FoldMarker> joined = first;
        for (const FoldMarker& f : second) {
            joined.append(FoldMarker{
                static_cast<int>(split) + f.column, f.length, f.regionId, f.isBegin});
        }
        QVERIFY2(markersEqual(fullMarkers, joined),
                 qPrintable(QStringLiteral("split at byte %1").arg(split)));
        QCOMPARE(s2, sFullOut);
    }
}

void TestFoldersInBytes::emptyBuffer_producesEmptyMarkers() {
    auto hl = makeBraceHighlighter();
    RuleBasedFoldingProvider prov(hl.get());
    HighlightState s0 = hl->initialState(), s1;
    QVector<FoldMarker> markers;
    prov.foldersInBytes(nullptr, 0, s0, markers, s1);
    QVERIFY(markers.isEmpty());
    QCOMPARE(s1, s0);
}

void TestFoldersInBytes::trailingNewline_matchesOneShot() {
    auto hl = makeBraceHighlighter();
    RuleBasedFoldingProvider prov(hl.get());

    const QByteArray a = QByteArrayLiteral("{ x }\n");
    const QByteArray b = QByteArrayLiteral("{ x }");
    HighlightState s0 = hl->initialState(), s1;
    QVector<FoldMarker> markers1, markers2;
    HighlightState s2;
    prov.foldersInBytes(a.constData(), a.size(), s0, markers1, s1);
    prov.foldersInBytes(b.constData(), b.size(), s0, markers2, s2);
    QVERIFY(markersEqual(markers1, markers2));
}

QTEST_GUILESS_MAIN(TestFoldersInBytes)
#include "test_folders_in_bytes.moc"
