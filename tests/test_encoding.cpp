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

    void lineBreakKinds() {
        const DecodeResult mac = decode(QByteArray("one\rtwo\r"));
        QVERIFY(mac.format.cr && !mac.format.crlf && !mac.format.mixedLineBreaks);
        QCOMPARE(mac.text, QStringLiteral("one\ntwo\n"));
        SimpleTextDocument doc;
        doc.setText(mac.text);
        QCOMPARE(doc.lineCount(), 2);
        QCOMPARE(encode(doc.toPlainText(), mac.format).bytes, QByteArray("one\rtwo\r"));

        // Mixed: reported, and saved with the most frequent kind (CRLF here).
        const DecodeResult mixed = decode(QByteArray("a\r\nb\r\nc\nd\re\r\n"));
        QVERIFY(mixed.format.mixedLineBreaks && mixed.format.crlf && !mixed.format.cr);
        doc.setText(mixed.text);
        QCOMPARE(doc.lineCount(), 5);
        QCOMPARE(encode(doc.toPlainText(), mixed.format).bytes, QByteArray("a\r\nb\r\nc\r\nd\r\ne\r\n"));

        const FileFormat windows = decode(QByteArray("x\r\n")).format;
        QVERIFY(windows.crlf && !windows.cr && !windows.mixedLineBreaks);
        const FileFormat unix = decode(QByteArray("x\n")).format;
        QVERIFY(!unix.crlf && !unix.cr && !unix.mixedLineBreaks);
    }

    // A language hint limits legacy code pages to that language's; UTF still wins.
    void decodeWithLanguageHint() {
        // A short line: without the hint the language could be misread.
        const QByteArray shortCp1250("// zmiana \xBF\xF3\xB3tego koloru\n");
        const DecodeResult hinted = decode(shortCp1250, {}, QStringLiteral("pl"));
        QVERIFY(hinted.ok);
        QCOMPARE(hinted.format.encoding, QStringLiteral("cp1250"));
        QCOMPARE(hinted.text, QStringLiteral("// zmiana żółtego koloru\n"));
        QCOMPARE(decode(kPolish.toUtf8(), {}, QStringLiteral("pl")).format.encoding, QStringLiteral("utf8"));

        const QList<Language> languages = availableLanguages();
        QVERIFY(languages.size() > 20);
        const auto polish = std::find_if(languages.begin(), languages.end(),
                                         [](const Language& l) { return l.code == QLatin1String("pl"); });
        QVERIFY(polish != languages.end());
        QCOMPARE(polish->name, QStringLiteral("Polish"));
    }

    void detectsLanguage() {
        preloadDetectionData(); // detection below waits for it if still loading
        const Language polish = detectLanguage(kPolishCp1250);
        QCOMPARE(polish.code, QStringLiteral("pl"));
        QCOMPARE(polish.name, QStringLiteral("Polish"));
        QCOMPARE(detectLanguage(kPolish.toUtf8()).code, QStringLiteral("pl"));
        QVERIFY(detectLanguage(QByteArray()).code.isEmpty());

        QObject context;
        Language async;
        bool called = false;
        detectLanguageAsync(kPolishCp1250, &context, [&](const Language& l) { async = l; called = true; });
        QTRY_VERIFY(called);
        QCOMPARE(async.code, QStringLiteral("pl"));
    }

    // decodeExact() / encodeExact() keep every line break as it is.
    void exactKeepsMixedLineBreaks() {
        const QByteArray bytes("a\r\nb\nc\rd\xEA\r\n");
        const DecodeResult r = decodeExact(bytes, QStringLiteral("cp1250"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, QStringLiteral("a\r\nb\nc\rdę\r\n"));
        QVERIFY(r.format.crlf);
        QVERIFY(r.format.finalNewline);
        const EncodeResult e = encodeExact(r.text, QStringLiteral("cp1250"));
        QVERIFY(e.ok);
        QCOMPARE(e.bytes, bytes);
    }

    void exactKeepsBom() {
        const QByteArray bytes("\xEF\xBB\xBFx\r\n");
        const DecodeResult r = decodeExact(bytes);
        QVERIFY(r.ok);
        QCOMPARE(r.format.encoding, QStringLiteral("utf8"));
        QVERIFY(r.format.bom);
        QCOMPARE(r.text, QStringLiteral("x\r\n"));
        QCOMPARE(encodeExact(r.text, r.format.encoding, r.format.bom).bytes, bytes);
    }

    void guardEncodesExactText() {
        Editor e;
        e.answer = EncodingGuard::Choice::Replace;
        QByteArray bytes;
        QVERIFY(e.guard.encodeForSave(QStringLiteral("ł\r\n☺\n"), &bytes, true));
        QCOMPARE(bytes, QByteArray("\xB3\r\n?\n"));
        QCOMPARE(e.asked.size(), 1);
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

    void longCp1250TextIsConfident() {
        const EncodingDetection d = detectEncoding(kPolishCp1250);
        QVERIFY(d.confident);
        QCOMPARE(d.options.size(), 1);
        QCOMPARE(d.options.front().encoding, QStringLiteral("cp1250"));
        QCOMPARE(d.options.front().language, QStringLiteral("pl"));
        QVERIFY(d.options.front().confidence >= 0.95);
    }

    void utf8AsciiAndEmptyAreConfident() {
        for (const QByteArray& bytes : {kPolish.toUtf8(), QByteArray("plain ascii\n"), QByteArray()}) {
            const EncodingDetection d = detectEncoding(bytes);
            QVERIFY(d.confident);
            QCOMPARE(d.options.size(), 1);
            QCOMPARE(d.options.front().encoding, QStringLiteral("utf8"));
        }
        // UTF-16 with a byte order mark is certain too.
        QByteArray utf16("\xFF\xFE");
        for (const QChar c : kPolish) utf16 += char(c.unicode() & 0xFF), utf16 += char(c.unicode() >> 8);
        const EncodingDetection d = detectEncoding(utf16);
        QVERIFY(d.confident);
        QCOMPARE(d.options.size(), 1);
        QVERIFY(d.options.front().encoding.startsWith(QLatin1String("utf16")));
    }

    // One Polish letter among source lines is little evidence: several options.
    void fewNationalLettersAreUncertain() {
        QByteArray source;
        for (int i = 0; i < 20; ++i)
            source += "  Form" + QByteArray::number(i) + ".Left := " + QByteArray::number(i * 8) + ";\n";
        source += "  OpenDial.Caption:='Wska\xBF program do symulacji';\n";
        const EncodingDetection d = detectEncoding(source);
        QVERIFY(!d.confident);
        QVERIFY(d.options.size() > 1);
        double sum = 0;
        bool hasCp1250 = false;
        for (qsizetype i = 0; i < d.options.size(); ++i) {
            if (i > 0) QVERIFY(d.options[i - 1].confidence >= d.options[i].confidence);
            sum += d.options[i].confidence;
            hasCp1250 = hasCp1250 || d.options[i].encoding == QLatin1String("cp1250");
        }
        QVERIFY(hasCp1250);
        QVERIFY(sum >= 0.95 - 1e-9);
    }

    void languagesRestrictTheOptions() {
        const EncodingDetection d = detectEncoding(kPolishCp1250, {QStringLiteral("pl"), QStringLiteral("cz")});
        QVERIFY(!d.options.isEmpty());
        for (const EncodingOption& option : d.options)
            QVERIFY(option.language == QLatin1String("pl") || option.language == QLatin1String("cz"));
        QCOMPARE(detect(kPolishCp1250, QStringList{QStringLiteral("pl"), QStringLiteral("cz")}),
                 QStringLiteral("cp1250"));
    }

    void decodeReportsDetectionOnlyWhenDetecting() {
        const DecodeResult detected = decode(kPolishCp1250);
        QVERIFY(detected.ok);
        QVERIFY(!detected.detection.options.isEmpty());
        QCOMPARE(detected.detection.options.front().encoding, detected.format.encoding);
        const DecodeResult given = decode(kPolishCp1250, QStringLiteral("cp1250"));
        QVERIFY(given.ok);
        QVERIFY(given.detection.options.isEmpty());
        const DecodeResult listed = decode(kPolishCp1250, QString(), QStringList{QStringLiteral("pl")});
        QCOMPARE(listed.format.encoding, QStringLiteral("cp1250"));
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
