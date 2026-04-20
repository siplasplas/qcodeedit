#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>
#include <qce/Utf8Map.h>

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QtTest/QtTest>

using namespace qce;

/// Trivial "C-like" highlighter: keyword "int" (attr 0) and multi-line
/// comment /* ... */ (attr 1). Mirrors test_highlighter_integration.cpp.
static std::unique_ptr<RulesHighlighter> makeCLike() {
    auto hl = std::make_unique<RulesHighlighter>();
    const int attrKw  = hl->addAttribute({QColor("purple")});
    const int attrCom = hl->addAttribute({QColor("olive")});

    const int klId = hl->addKeywordList({"kw", {QStringLiteral("int")}, true});

    const int ctxNormal = hl->addContext({"Normal",  -1, -1, 0, false, -1, {}});
    const int ctxCom    = hl->addContext({"Comment", attrCom, -1, 0, false, -1, {}});

    hl->contextRef(ctxNormal).rules.push_back(
        {HighlightRule::Keyword, {}, {}, {}, true, {}, klId, -1,
         attrKw, -1, 0, false, false});
    hl->contextRef(ctxNormal).rules.push_back(
        {HighlightRule::Detect2Chars, '/', '*', {}, true, {}, -1, -1,
         attrCom, ctxCom, 0, false, false});
    hl->contextRef(ctxCom).rules.push_back(
        {HighlightRule::Detect2Chars, '*', '/', {}, true, {}, -1, -1,
         attrCom, -1, 1, false, false});
    return hl;
}

static bool spansEqual(const QVector<StyleSpan>& a, const QVector<StyleSpan>& b) {
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].start != b[i].start
            || a[i].length != b[i].length
            || a[i].attributeId != b[i].attributeId) return false;
    }
    return true;
}

class TestTokenizeBytes : public QObject {
    Q_OBJECT
private slots:
    void roundTrip_singleLine_matchesHighlightLine();
    void roundTrip_utf8_bytePositionsDecodeToSameSubstring();
    void lineSplitInvariance_oneShotEqualsConcatenatedPerLine();
    void resumability_lineBoundarySplitsThreadStateCorrectly();
    void emptyBuffer();
    void trailingNewline();
    void stateCarriedAcrossLines();
};

/// §3.1 — for a hand-written highlighter that only implements highlightLine(),
/// the default tokenizeBytes() produces spans whose byte ranges decode back
/// to the same QChar ranges.
void TestTokenizeBytes::roundTrip_singleLine_matchesHighlightLine() {
    auto hl = makeCLike();
    const QString line = QStringLiteral("int a; int b;");
    const QByteArray utf8 = line.toUtf8();

    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> qcharSpans;
    hl->highlightLine(line, s0, qcharSpans, s1);

    HighlightState s0b = hl->initialState(), s1b;
    QVector<StyleSpan> byteSpans;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), s0b, byteSpans, s1b);

    QCOMPARE(s1b, s1);
    QCOMPARE(byteSpans.size(), qcharSpans.size());

    // For ASCII-only content the byte and QChar positions coincide.
    for (int i = 0; i < byteSpans.size(); ++i) {
        QCOMPARE(byteSpans[i].start,       qcharSpans[i].start);
        QCOMPARE(byteSpans[i].length,      qcharSpans[i].length);
        QCOMPARE(byteSpans[i].attributeId, qcharSpans[i].attributeId);
    }
}

void TestTokenizeBytes::roundTrip_utf8_bytePositionsDecodeToSameSubstring() {
    auto hl = makeCLike();
    // Non-ASCII content around a keyword so byte ≠ QChar.
    const QString line = QStringLiteral("x=€; int y;");
    const QByteArray utf8 = line.toUtf8();

    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> qcharSpans;
    hl->highlightLine(line, s0, qcharSpans, s1);

    HighlightState s0b = hl->initialState(), s1b;
    QVector<StyleSpan> byteSpans;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), s0b, byteSpans, s1b);

    QCOMPARE(byteSpans.size(), qcharSpans.size());

    // Decode each byte span and compare to the QChar span it represents.
    for (int i = 0; i < byteSpans.size(); ++i) {
        const StyleSpan& bs = byteSpans[i];
        const StyleSpan& qs = qcharSpans[i];
        const QString byteSub = QString::fromUtf8(utf8.constData() + bs.start, bs.length);
        const QString qcharSub = line.mid(qs.start, qs.length);
        QCOMPARE(byteSub, qcharSub);
        QCOMPARE(bs.attributeId, qs.attributeId);
    }
}

/// §3.2 — tokenising a multi-line UTF-8 buffer as one range equals
/// concatenating per-line tokenisations (with correct byte offsets).
void TestTokenizeBytes::lineSplitInvariance_oneShotEqualsConcatenatedPerLine() {
    auto hl = makeCLike();
    const QString text = QStringLiteral("int a;\nint b;\nint c;");
    const QByteArray utf8 = text.toUtf8();

    // One-shot over the whole buffer.
    HighlightState sA = hl->initialState(), sAOut;
    QVector<StyleSpan> oneShot;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), sA, oneShot, sAOut);

    // Per-line tokenisation with manual offset stitching.
    QVector<StyleSpan> stitched;
    HighlightState s = hl->initialState();
    qsizetype base = 0;
    const QByteArrayList lines = utf8.split('\n');
    for (int li = 0; li < lines.size(); ++li) {
        const QByteArray& ln = lines[li];
        QVector<StyleSpan> lineSpans;
        HighlightState sNext;
        hl->tokenizeBytes(ln.constData(), ln.size(), s, lineSpans, sNext);
        for (const StyleSpan& sp : lineSpans) {
            stitched.append(StyleSpan{
                static_cast<int>(base + sp.start), sp.length, sp.attributeId});
        }
        s = sNext;
        base += ln.size() + (li + 1 < lines.size() ? 1 : 0);  // +1 for '\n'
    }

    QVERIFY(spansEqual(oneShot, stitched));
    QCOMPARE(sAOut, s);
}

/// §3.3 — splitting at line boundaries and threading stateOut → stateIn
/// yields the same spans as a single one-shot call. (The default impl
/// resumes at line granularity; mid-line resumability would require a
/// native byte override, which is out of scope for the MVP.)
void TestTokenizeBytes::resumability_lineBoundarySplitsThreadStateCorrectly() {
    auto hl = makeCLike();
    // Multi-line comment spans the middle two lines — state must thread.
    const QString text = QStringLiteral(
        "int a; /* open\n"
        "still in comment\n"
        "still in comment\n"
        "end */ int b;");
    const QByteArray utf8 = text.toUtf8();

    HighlightState sA = hl->initialState(), sAOut;
    QVector<StyleSpan> oneShot;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), sA, oneShot, sAOut);

    // Find every '\n' offset — these are the legal split points for the
    // default (line-delegating) implementation.
    QVector<qsizetype> nl;
    for (qsizetype i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '\n') nl.append(i + 1);  // split after the newline
    }

    for (qsizetype split : nl) {
        HighlightState s0 = hl->initialState(), s1, s2;
        QVector<StyleSpan> first, second;
        hl->tokenizeBytes(utf8.constData(), split, s0, first, s1);
        hl->tokenizeBytes(utf8.constData() + split,
                          utf8.size() - split, s1, second, s2);

        // Re-offset the second half's spans and concatenate.
        QVector<StyleSpan> joined = first;
        for (const StyleSpan& sp : second) {
            joined.append(StyleSpan{
                static_cast<int>(split + sp.start), sp.length, sp.attributeId});
        }

        QVERIFY2(spansEqual(oneShot, joined),
                 qPrintable(QStringLiteral("split=%1").arg(split)));
        QCOMPARE(s2, sAOut);
    }
}

void TestTokenizeBytes::emptyBuffer() {
    auto hl = makeCLike();
    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> spans;
    hl->tokenizeBytes(nullptr, 0, s0, spans, s1);
    QVERIFY(spans.isEmpty());
    QCOMPARE(s1, s0);
}

void TestTokenizeBytes::trailingNewline() {
    auto hl = makeCLike();
    const QByteArray utf8 = QByteArrayLiteral("int a;\n");
    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> spans;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), s0, spans, s1);

    // "int" keyword should be present with byte offsets 0..3.
    bool found = false;
    for (const auto& sp : spans) {
        if (sp.start == 0 && sp.length == 3) { found = true; break; }
    }
    QVERIFY(found);

    // State stable (back to Normal) — comparable to one-shot of just "int a;".
    HighlightState s2;
    QVector<StyleSpan> spans2;
    hl->tokenizeBytes("int a;", 6, s0, spans2, s2);
    QCOMPARE(s1, s2);
}

/// State flows across '\n' in tokenizeBytes the same way it flows across
/// highlightLine() calls — a /* opened on line 0 is still open on line 1.
void TestTokenizeBytes::stateCarriedAcrossLines() {
    auto hl = makeCLike();
    const QByteArray utf8 = QByteArrayLiteral("a /* open\nstill in\nend */");
    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> spans;
    hl->tokenizeBytes(utf8.constData(), utf8.size(), s0, spans, s1);

    // Middle line "still in" should be entirely one comment span.
    // Find spans whose start is in the middle-line byte range [10, 18).
    bool middleIsOneComment = false;
    for (const auto& sp : spans) {
        if (sp.start == 10 && sp.length == 8 && sp.attributeId == 1) {
            middleIsOneComment = true;
            break;
        }
    }
    QVERIFY(middleIsOneComment);
    // After "*/" on the last line, we're back in Normal.
    QCOMPARE(s1, s0);
}

QTEST_GUILESS_MAIN(TestTokenizeBytes)
#include "test_tokenize_bytes.moc"
