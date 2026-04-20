#include <qce/Utf8Map.h>

#include <QByteArray>
#include <QString>
#include <QtTest/QtTest>

class TestUtf8Map : public QObject {
    Q_OBJECT

private slots:
    void empty();
    void ascii();
    void twoByte();
    void threeByte();
    void fourByte_surrogatePair();
    void mixed();
    void pastTheEnd();
    void qstringRoundTrip();
};

void TestUtf8Map::empty() {
    qce::Utf8Map m;
    m.buildFromUtf8(nullptr, 0);
    QCOMPARE(m.qcharCount(), 0);
    QCOMPARE(m.byteCount(),  qsizetype(0));
    QCOMPARE(m.byteForQChar(0), qsizetype(0));
    QCOMPARE(m.qcharForByte(0), 0);
}

void TestUtf8Map::ascii() {
    const QByteArray s = "hello";
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    QCOMPARE(m.qcharCount(), 5);
    QCOMPARE(m.byteCount(),  qsizetype(5));
    for (int i = 0; i <= 5; ++i) {
        QCOMPARE(m.byteForQChar(i), qsizetype(i));
        QCOMPARE(m.qcharForByte(i), i);
    }
}

void TestUtf8Map::twoByte() {
    // "á" = 0xC3 0xA1 (1 QChar), then "b".
    const QByteArray s = QString::fromUtf8("áb").toUtf8();
    QCOMPARE(s.size(), 3);
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    QCOMPARE(m.qcharCount(), 2);
    QCOMPARE(m.byteCount(),  qsizetype(3));
    // 'á' → QChar 0, bytes 0..1
    QCOMPARE(m.byteForQChar(0), qsizetype(0));
    QCOMPARE(m.qcharForByte(0), 0);
    QCOMPARE(m.qcharForByte(1), 0);  // interior byte of á folds to its code point
    // 'b' → QChar 1, byte 2
    QCOMPARE(m.byteForQChar(1), qsizetype(2));
    QCOMPARE(m.qcharForByte(2), 1);
    // Past-the-end
    QCOMPARE(m.byteForQChar(2), qsizetype(3));
    QCOMPARE(m.qcharForByte(3), 2);
}

void TestUtf8Map::threeByte() {
    // "€" = 0xE2 0x82 0xAC (1 QChar), then "x".
    const QByteArray s = QString::fromUtf8("€x").toUtf8();
    QCOMPARE(s.size(), 4);
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    QCOMPARE(m.qcharCount(), 2);
    QCOMPARE(m.byteForQChar(0), qsizetype(0));
    QCOMPARE(m.byteForQChar(1), qsizetype(3));
    QCOMPARE(m.byteForQChar(2), qsizetype(4));
    QCOMPARE(m.qcharForByte(0), 0);
    QCOMPARE(m.qcharForByte(2), 0);
    QCOMPARE(m.qcharForByte(3), 1);
    QCOMPARE(m.qcharForByte(4), 2);
}

void TestUtf8Map::fourByte_surrogatePair() {
    // U+1F600 (😀) = F0 9F 98 80 (4 bytes, 2 QChars = surrogate pair),
    // then ASCII 'a'.
    const QByteArray s = QString::fromUtf8("😀a").toUtf8();
    QCOMPARE(s.size(), 5);
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    QCOMPARE(m.qcharCount(), 3);          // 2 surrogates + 'a'
    QCOMPARE(m.byteCount(),  qsizetype(5));
    // Both surrogate halves map to byte 0 (the code point's start).
    QCOMPARE(m.byteForQChar(0), qsizetype(0));
    QCOMPARE(m.byteForQChar(1), qsizetype(0));
    // 'a' at QChar 2, byte 4.
    QCOMPARE(m.byteForQChar(2), qsizetype(4));
    QCOMPARE(m.byteForQChar(3), qsizetype(5));
    // All 4 bytes of the emoji map to QChar 0 (the high surrogate).
    for (qsizetype b = 0; b < 4; ++b) {
        QCOMPARE(m.qcharForByte(b), 0);
    }
    QCOMPARE(m.qcharForByte(4), 2);

    // A span covering the full code point (start=0, length=2) maps to the
    // full 4 UTF-8 bytes. A degenerate span that stops between the
    // surrogate halves (start=0, length=1) has no UTF-8 representation —
    // it collapses to an empty byte range at the code point's start.
    QCOMPARE(m.byteForQChar(0 + 2) - m.byteForQChar(0), qsizetype(4));
    QCOMPARE(m.byteForQChar(0 + 1) - m.byteForQChar(0), qsizetype(0));
}

void TestUtf8Map::mixed() {
    // No 4-byte sequences so every QChar round-trips losslessly through
    // a byte offset. Surrogate-pair semantics are tested separately.
    const QString qs = QStringLiteral("A€B\u00E1C");
    const QByteArray s = qs.toUtf8();
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    QCOMPARE(m.qcharCount(), qs.size());

    for (int i = 0; i <= qs.size(); ++i) {
        const qsizetype b = m.byteForQChar(i);
        const int i2 = m.qcharForByte(b);
        QCOMPARE(i2, i);
    }
}

void TestUtf8Map::pastTheEnd() {
    const QByteArray s = "ab";
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());
    // Out-of-range should clamp, not crash or read past.
    QCOMPARE(m.byteForQChar(-5), qsizetype(0));
    QCOMPARE(m.byteForQChar(99), qsizetype(2));
    QCOMPARE(m.qcharForByte(-5), 0);
    QCOMPARE(m.qcharForByte(99), 2);
}

void TestUtf8Map::qstringRoundTrip() {
    // Each boundary either falls before or after a whole code point —
    // never between the halves of a surrogate pair — so the byte range
    // between any two boundaries decodes to the same substring.
    const QString qs = QStringLiteral("prefix €uro done");
    const QByteArray s = qs.toUtf8();
    qce::Utf8Map m;
    m.buildFromUtf8(s.constData(), s.size());

    for (int i = 0; i <= qs.size(); ++i) {
        for (int j = i; j <= qs.size(); ++j) {
            const qsizetype bi = m.byteForQChar(i);
            const qsizetype bj = m.byteForQChar(j);
            const QString sub = QString::fromUtf8(s.constData() + bi, bj - bi);
            QCOMPARE(sub, qs.mid(i, j - i));
        }
    }
}

QTEST_GUILESS_MAIN(TestUtf8Map)
#include "test_utf8_map.moc"
