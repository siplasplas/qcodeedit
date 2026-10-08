#include <qce/encoding/Encoding.h>
#include <qce/encoding/EncodingGuard.h>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>

using namespace qce;
using namespace qce::encoding;

// "Zażółć gęślą jaźń" in cp1250: ż=BF, ó=F3, ł=B3, ć=E6, ę=EA, ś=9C, ą=B9, ź=9F, ń=F1.
static const QByteArray kPolishCp1250(
    "Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1. "
    "W \xBF\xF3\xB3tej ksi\xB9\xBF" "ce zapisano, \xBF" "e pszcz\xF3\xB3ka "
    "siedzia\xB3" "a na \x9Cwie\xBF" "ym kwiatku i zbiera\xB3" "a mi\xF3" "d.\r\n");
static const QString kPolish = QStringLiteral(
    "Zażółć gęślą jaźń. W żółtej książce zapisano, że pszczółka "
    "siedziała na świeżym kwiatku i zbierała miód.");

class TestEncoding : public QObject {
    Q_OBJECT

private:
    struct Editor {
        SimpleTextDocument doc;
        CodeEditArea area;
        EncodingGuard guard{&area};
        QList<QList<char32_t>> asked;
        EncodingGuard::Choice answer = EncodingGuard::Choice::Cancel;

        Editor() {
            doc.setText(QStringLiteral("abc"));
            area.setDocument(&doc);
            guard.setFormat({QStringLiteral("cp1250"), false, false, false});
            guard.setChoiceHandler([this](const QList<char32_t>& chars, const QString&) {
                asked << chars;
                return answer;
            });
            area.show();
            area.setFocus();
            QCoreApplication::processEvents();
            QTest::keyClick(&area, Qt::Key_End);
        }

        // QTest::keyClicks handles ASCII only; send the text like a keyboard.
        void type(const QString& text) {
            for (const QChar c : text) {
                QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
                QCoreApplication::sendEvent(&area, &press);
            }
        }

        void paste(const QString& text) {
            QGuiApplication::clipboard()->setText(text);
            QTest::keyClick(&area, Qt::Key_V, Qt::ControlModifier);
        }
    };

private slots:
    void decodeExplicitCodePage() {
        const DecodeResult r = decode(kPolishCp1250, QStringLiteral("cp1250"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, kPolish + QLatin1Char('\n'));
        QCOMPARE(r.format.encoding, QStringLiteral("cp1250"));
        QVERIFY(r.format.crlf);
        QVERIFY(r.format.finalNewline);
        QVERIFY(!r.format.bom);
    }

    // decode() -> SimpleTextDocument -> encode() gives the file back byte for
    // byte, including empty last lines and a missing final line break.
    void roundTripThroughDocument_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("encoding");
        QTest::newRow("cp1250 crlf") << kPolishCp1250 << QStringLiteral("cp1250");
        QTest::newRow("lf") << QByteArray("one\ntwo\n") << QString();
        QTest::newRow("crlf") << QByteArray("one\r\ntwo\r\n") << QString();
        QTest::newRow("no final newline") << QByteArray("one\ntwo") << QString();
        QTest::newRow("empty last line") << QByteArray("one\n\n") << QString();
        QTest::newRow("empty last line crlf") << QByteArray("one\r\n\r\n") << QString();
        QTest::newRow("empty") << QByteArray() << QString();
        QTest::newRow("newline only") << QByteArray("\n") << QString();
        QTest::newRow("two newlines") << QByteArray("\n\n") << QString();
        QTest::newRow("utf8 bom") << QByteArray("\xEF\xBB\xBFza\xC5\xBC\n") << QString();
        QTest::newRow("latin1") << QByteArray("caf\xE9 \xFF\n") << QStringLiteral("iso-8859-1");
    }

    void roundTripThroughDocument() {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, encoding);
        const DecodeResult r = decode(bytes, encoding);
        QVERIFY(r.ok);
        SimpleTextDocument doc;
        doc.setText(r.text);
        const EncodeResult e = encode(doc.toPlainText(), r.format);
        QVERIFY(e.ok);
        QCOMPARE(e.bytes, bytes);
    }

    void detectsPolishCp1250() {
        QCOMPARE(detect(kPolishCp1250), QStringLiteral("cp1250"));
        QCOMPARE(detect(kPolishCp1250, QStringLiteral("pl")), QStringLiteral("cp1250"));
        const DecodeResult r = decode(kPolishCp1250);
        QVERIFY(r.ok);
        QCOMPARE(r.format.encoding, QStringLiteral("cp1250"));
        QCOMPARE(r.text, kPolish + QLatin1Char('\n'));
    }

    void detectsUtf8AndAscii() {
        QCOMPARE(detect(QByteArray("plain ascii\n")), QStringLiteral("utf8"));
        QCOMPARE(detect(kPolish.toUtf8()), QStringLiteral("utf8"));
        QCOMPARE(detect(QByteArray()), QStringLiteral("utf8"));
    }

    void utf8BomIsRemembered() {
        const DecodeResult r = decode(QByteArray("\xEF\xBB\xBFzażółć\n"), QStringLiteral("utf8"));
        QVERIFY(r.ok);
        QVERIFY(r.format.bom);
        QCOMPARE(r.text, QStringLiteral("zażółć\n"));
        QCOMPARE(encode(QStringLiteral("zażółć"), r.format).bytes,
                 QByteArray("\xEF\xBB\xBFzażółć\n"));
    }

    void invalidBytesFailForExplicitEncoding() {
        const DecodeResult r = decode(QByteArray("ab\xFF"), QStringLiteral("utf8"));
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void encodeRejectsOrReplacesUnrepresentable() {
        const FileFormat cp1250{QStringLiteral("cp1250"), false, false, false};
        const EncodeResult rejected = encode(QStringLiteral("łódź € ☺ ☺ 漢"), cp1250);
        QVERIFY(!rejected.ok);
        QCOMPARE(rejected.unrepresentable, (QList<char32_t>{U'☺', U'漢'}));
        const EncodeResult replaced = encode(QStringLiteral("ł☺"), cp1250, true);
        QVERIFY(replaced.ok);
        QCOMPARE(replaced.bytes, QByteArray("\xB3?"));
        QCOMPARE(replaceUnrepresentable(QStringLiteral("a☺ł漢"), QStringLiteral("cp1250")),
                 QStringLiteral("a?ł?"));
        QVERIFY(unrepresentable(QStringLiteral("☺漢"), QStringLiteral("utf8")).isEmpty());
    }

    void guardAcceptsRepresentableText() {
        Editor e;
        e.paste(QStringLiteral("ąę"));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abcąę"));
        QVERIFY(e.asked.isEmpty());
    }

    void guardReplaceWritesQuestionMarks() {
        Editor e;
        e.answer = EncodingGuard::Choice::Replace;
        e.paste(QStringLiteral("ł☺"));
        QCOMPARE(e.asked, (QList<QList<char32_t>>{{U'☺'}}));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abcł?"));
        QCOMPARE(e.guard.encoding(), QStringLiteral("cp1250"));
    }

    void guardSwitchToUtf8KeepsText() {
        Editor e;
        e.answer = EncodingGuard::Choice::SwitchToUtf8;
        QSignalSpy changed(&e.guard, &EncodingGuard::encodingChanged);
        e.paste(QStringLiteral("☺"));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abc☺"));
        QCOMPARE(e.guard.encoding(), QStringLiteral("utf8"));
        QCOMPARE(changed.count(), 1);
        // Further Unicode input is accepted without asking.
        e.paste(QStringLiteral("漢"));
        QCOMPARE(e.asked.size(), 1);
        QByteArray bytes;
        QVERIFY(e.guard.encodeForSave(e.doc.toPlainText(), &bytes));
        QCOMPARE(bytes, QStringLiteral("abc☺漢").toUtf8());
    }

    void guardCancelInsertsNothing() {
        Editor e;
        e.answer = EncodingGuard::Choice::Cancel;
        e.paste(QStringLiteral("x☺"));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abc"));
        e.type(QStringLiteral("ó"));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abcó"));
    }

    void guardChecksTypedText() {
        Editor e;
        e.answer = EncodingGuard::Choice::Cancel;
        e.type(QStringLiteral("€☺"));
        QCOMPARE(e.doc.lineAt(0), QStringLiteral("abc€"));
        QCOMPARE(e.asked.size(), 1);
    }

    void saveAsksForTextThatBypassedTheGuard() {
        Editor e;
        e.doc.setText(QStringLiteral("x☺")); // not through the editor
        QByteArray bytes;
        e.answer = EncodingGuard::Choice::Cancel;
        QVERIFY(!e.guard.encodeForSave(e.doc.toPlainText(), &bytes));
        e.answer = EncodingGuard::Choice::Replace;
        QVERIFY(e.guard.encodeForSave(e.doc.toPlainText(), &bytes));
        QCOMPARE(bytes, QByteArray("x?"));
    }
};

QTEST_MAIN(TestEncoding)
#include "test_encoding.moc"
