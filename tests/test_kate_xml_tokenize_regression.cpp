#include <qce/kate/KateXmlReader.h>

#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QtTest/QtTest>

#ifndef QCE_TEST_DATA_DIR
#  error "QCE_TEST_DATA_DIR must be set by CMake"
#endif

/// HANDOFF §3.4 — Kate XML regression.
///
/// Load Kate's XML highlighter (xml.xml) and tokenise books.xml via the
/// default tokenizeBytes() path. Asserts structural stability only: we
/// don't lock a brittle snapshot of span content (that would break every
/// time KDE ships a new xml.xml), but we do verify the properties the
/// viewer relies on — determinism, non-overlap, byte-boundedness, and
/// agreement between one-shot and per-line tokenisation.
class TestKateXmlTokenizeRegression : public QObject {
    Q_OBJECT
private slots:
    void booksXml_tokenizesStably();
    void booksXml_noErrorSpans();
};

static QString kateSyntaxPath(const QString& fileName) {
    const QString p = QDir::homePath()
        + QStringLiteral("/.local/share/org.kde.syntax-highlighting/syntax/")
        + fileName;
    return QFile::exists(p) ? p : QString();
}

void TestKateXmlTokenizeRegression::booksXml_tokenizesStably() {
    const QString xmlXml = kateSyntaxPath(QStringLiteral("xml.xml"));
    if (xmlXml.isEmpty()) QSKIP("xml.xml not installed on this system");

    auto hl = KateXmlReader::load(xmlXml);
    QVERIFY(hl != nullptr);

    const QString booksPath = QStringLiteral(QCE_TEST_DATA_DIR "/books.xml");
    QFile f(booksPath);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(booksPath));
    const QByteArray buf = f.readAll();
    QVERIFY(buf.size() > 0);

    // --- One-shot tokenisation ------------------------------------------
    qce::HighlightState s0 = hl->initialState(), s1;
    QVector<qce::StyleSpan> spans;
    hl->tokenizeBytes(buf.constData(), buf.size(), s0, spans, s1);

    QVERIFY2(!spans.isEmpty(), "xml.xml should produce at least some spans");

    // --- Property checks ------------------------------------------------
    // 1. Spans are sorted by start and non-overlapping.
    for (int i = 1; i < spans.size(); ++i) {
        QVERIFY2(spans[i - 1].start + spans[i - 1].length <= spans[i].start,
                 qPrintable(QStringLiteral("span %1 overlaps %2").arg(i - 1).arg(i)));
    }
    // 2. All spans are byte-bounded by the buffer.
    for (const auto& sp : spans) {
        QVERIFY(sp.start >= 0);
        QVERIFY(sp.length >= 0);
        QVERIFY(sp.start + sp.length <= buf.size());
    }
    // 3. No span crosses a '\n' — tokenizeBytes documents line-bounded spans.
    for (const auto& sp : spans) {
        const QByteArray slice = buf.mid(sp.start, sp.length);
        QVERIFY2(!slice.contains('\n'),
                 qPrintable(QStringLiteral("span at %1 crosses a newline").arg(sp.start)));
    }

    // --- Determinism: a second run yields byte-identical output --------
    qce::HighlightState s0b = hl->initialState(), s1b;
    QVector<qce::StyleSpan> spans2;
    hl->tokenizeBytes(buf.constData(), buf.size(), s0b, spans2, s1b);
    QCOMPARE(spans.size(), spans2.size());
    QCOMPARE(s1, s1b);
    for (int i = 0; i < spans.size(); ++i) {
        QCOMPARE(spans[i].start, spans2[i].start);
        QCOMPARE(spans[i].length, spans2[i].length);
        QCOMPARE(spans[i].attributeId, spans2[i].attributeId);
    }

    // --- Line-split invariance on a real file --------------------------
    // Per-line tokenisation threaded by state must match the one-shot run.
    QVector<qce::StyleSpan> stitched;
    qce::HighlightState s = hl->initialState();
    qsizetype base = 0;
    const QList<QByteArray> lines = buf.split('\n');
    for (int li = 0; li < lines.size(); ++li) {
        const QByteArray& ln = lines[li];
        QVector<qce::StyleSpan> lineSpans;
        qce::HighlightState sNext;
        hl->tokenizeBytes(ln.constData(), ln.size(), s, lineSpans, sNext);
        for (const qce::StyleSpan& sp : lineSpans) {
            stitched.append(qce::StyleSpan{
                static_cast<int>(base + sp.start), sp.length, sp.attributeId});
        }
        s = sNext;
        base += ln.size() + (li + 1 < lines.size() ? 1 : 0);
    }
    QCOMPARE(stitched.size(), spans.size());
    for (int i = 0; i < spans.size(); ++i) {
        QCOMPARE(stitched[i].start, spans[i].start);
        QCOMPARE(stitched[i].length, spans[i].length);
        QCOMPARE(stitched[i].attributeId, spans[i].attributeId);
    }
    QCOMPARE(s, s1);
}

/// HANDOFF_qcodeedit_dynamic.md §1 — before the dynamic="true" fix,
/// every opening tag past the first PI rendered as dsError (attribute
/// "Error") because the StringDetect String="%1" never matched.
/// Guard against regression by asserting no span under the Error attr.
void TestKateXmlTokenizeRegression::booksXml_noErrorSpans() {
    const QString xmlXml = kateSyntaxPath(QStringLiteral("xml.xml"));
    if (xmlXml.isEmpty()) QSKIP("xml.xml not installed on this system");

    auto hl = KateXmlReader::load(xmlXml);
    QVERIFY(hl != nullptr);

    // xml.xml's itemData order puts "Error" last. Look it up by name via
    // the deterministic index (see xml.xml <itemDatas>). We don't hardcode
    // 17 here — if KDE adds more itemDatas the index moves; instead we
    // scan for any span whose attribute is darker-red-bold (dsError
    // typically maps to #BF0303 bold), which is the visible symptom.
    // Simpler: accept that the palette order is stable and that "Error"
    // sits at attributes().size() - 1.
    const int attrError = hl->attributes().size() - 1;
    QVERIFY(attrError > 0);

    const QString booksPath = QStringLiteral(QCE_TEST_DATA_DIR "/books.xml");
    QFile f(booksPath);
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(booksPath));
    const QByteArray buf = f.readAll();

    qce::HighlightState s0 = hl->initialState(), s1;
    QVector<qce::StyleSpan> spans;
    hl->tokenizeBytes(buf.constData(), buf.size(), s0, spans, s1);

    for (const auto& sp : spans) {
        if (sp.attributeId == attrError) {
            const QByteArray slice = buf.mid(sp.start, sp.length);
            QFAIL(qPrintable(QStringLiteral("Error span at byte %1: %2")
                             .arg(sp.start).arg(QString::fromUtf8(slice))));
        }
    }
}

QTEST_GUILESS_MAIN(TestKateXmlTokenizeRegression)
#include "test_kate_xml_tokenize_regression.moc"
