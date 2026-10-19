#include <qce/spell/CodeEditSpelling.h>
#include <qce/spell/SpellChecker.h>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

#include "KateTestPaths.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUndoStack>
#include <QtTest>

using namespace qce;
using namespace qce::spell;

// Uses the system's hunspell dictionaries; tests needing Polish or English
// skip when they are not installed. The user dictionary goes to a temporary
// XDG_DATA_HOME, never to the real one.
class TestSpell : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;
    QString m_cppSyntax;

    static bool installed(const QString& language) { return !dictionaryFor(language).isEmpty(); }

private slots:
    void initTestCase() {
        // Found before XDG_DATA_HOME is changed: the Kate data is in the real one.
        m_cppSyntax = kateSyntaxPath(QStringLiteral("cpp.xml"));
        QVERIFY(m_home.isValid());
        qputenv("XDG_DATA_HOME", m_home.path().toLocal8Bit());
        // The suggestions' form files, too, stay out of the user's cache.
        qputenv("XDG_CACHE_HOME", m_home.filePath(QStringLiteral("cache")).toLocal8Bit());
    }

    void dictionariesAndLanguageKeys() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        QCOMPARE(dictionaryFor(QStringLiteral("pl")), QStringLiteral("pl_PL"));
        QCOMPARE(dictionaryFor(QStringLiteral("pl_PL")), QStringLiteral("pl_PL"));
        bool listed = false;
        for (const Dictionary& dictionary : installedDictionaries())
            if (dictionary.name == QLatin1String("pl_PL")) {
                listed = true;
                QCOMPARE(dictionary.language, QStringLiteral("pl"));
                QVERIFY(dictionary.displayName.startsWith(QStringLiteral("Polish")));
            }
        QVERIFY(listed);
        // cpg's key for Czech is "cz"; the dictionary is cs_*.
        const QString czech = dictionaryFor(QStringLiteral("cz"));
        QVERIFY(czech.isEmpty() || czech.startsWith(QStringLiteral("cs")));
        QCOMPARE(packageHint(QStringLiteral("cz")), QStringLiteral("hunspell-cs"));
        QCOMPARE(packageHint(QStringLiteral("de")), QStringLiteral("hunspell-de-de"));
        QVERIFY(dictionaryFor(QStringLiteral("xx")).isEmpty());
    }

    void findsMisspellingsAndSuggests() {
        if (!installed(QStringLiteral("pl")) || !installed(QStringLiteral("en")))
            QSKIP("Polish and English dictionaries needed");
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        QVERIFY(checker.isReady());
        QVERIFY(checker.check(QStringLiteral("książka")));
        QVERIFY(!checker.check(QStringLiteral("ksiązka")));
        QVERIFY(checker.suggestions(QStringLiteral("ksiązka")).contains(QStringLiteral("książka")));
        QVERIFY(!checker.check(QStringLiteral("spelling")));
        // Two languages: right in either.
        checker.setLanguages({QStringLiteral("en"), QStringLiteral("pl")});
        QCOMPARE(checker.dictionaries().size(), 2);
        QVERIFY(checker.check(QStringLiteral("spelling")));
        QVERIFY(checker.check(QStringLiteral("książka")));
        QVERIFY(!checker.check(QStringLiteral("speling")));
        QVERIFY(checker.suggestions(QStringLiteral("speling"), 8).size() <= 8);
        // Without dictionaries nothing is reported.
        SpellChecker none;
        QVERIFY(!none.isReady());
        QVERIFY(none.check(QStringLiteral("ksiązka")));
    }

    void skipsCodeAndAddresses() {
        const QString text = QStringLiteral(
            "Zwykłe słowa, camelCase PascalCase HTTP abc123 snake_case "
            "https://example.com/path a@b.com dir/file obj.method std::vector don't x");
        QStringList words;
        for (const auto& word : checkableWords(text, 0, int(text.size())))
            words << text.mid(word.first, word.second);
        QCOMPARE(words, (QStringList{QStringLiteral("Zwykłe"), QStringLiteral("słowa"), QStringLiteral("don't")}));
        // Only inside the given columns.
        QCOMPARE(checkableWords(text, 7, 12).size(), 1);
    }

    void checksOnlyCommentsAndStringsInCode() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        if (m_cppSyntax.isEmpty()) QSKIP("No Kate cpp.xml (qce-kate-fetch)");
        auto highlighter = KateXmlReader::load(m_cppSyntax, KateTheme{});
        QVERIFY(highlighter);
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("int blend = 0; // komentarz z błendem\nauto s = \"napis z błendem\";"));
        CodeEditArea area;
        area.setDocument(&doc);
        area.setHighlighter(highlighter.get());
        area.resize(600, 200);
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        spelling.recheck();
        const auto underlines = area.extraSelections(CodeEditArea::SpellingLayer);
        // "błendem" in the comment and in the string; "blend" (an identifier) not.
        QCOMPARE(underlines.size(), 2);
        for (const ExtraSelection& underline : underlines)
            QCOMPARE(doc.lineAt(underline.start.line).mid(underline.start.column,
                                                          underline.end.column - underline.start.column),
                     QStringLiteral("błendem"));
        area.setHighlighter(nullptr);
    }

    void underlinesSuggestsAndReplaces() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("To jest błend w tekście.\nhttps://example.com/błend"));
        CodeEditArea area;
        area.setDocument(&doc);
        area.resize(600, 200);
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        spelling.recheck();
        const auto underlines = area.extraSelections(CodeEditArea::SpellingLayer);
        QCOMPARE(underlines.size(), 1);
        QCOMPARE(underlines.front().start, (TextCursor{0, 8}));
        QCOMPARE(underlines.front().end, (TextCursor{0, 13}));
        QVERIFY(underlines.front().underline.isValid());

        const auto misspelling = spelling.misspelledWordAt({0, 10});
        QVERIFY(misspelling.has_value());
        QCOMPARE(misspelling->word, QStringLiteral("błend"));
        QVERIFY(!spelling.misspelledWordAt({0, 3}).has_value());  // "jest" is right
        spelling.replaceWord(*misspelling, QStringLiteral("błąd"));
        QCOMPARE(doc.lineAt(0), QStringLiteral("To jest błąd w tekście."));
        area.undoStack()->undo();
        QCOMPARE(doc.lineAt(0), QStringLiteral("To jest błend w tekście."));

        // Ignored in this editor only.
        spelling.ignore(QStringLiteral("błend"));
        QVERIFY(area.extraSelections(CodeEditArea::SpellingLayer).isEmpty());
        SpellChecker other;
        other.setLanguages({QStringLiteral("pl")});
        QVERIFY(!other.check(QStringLiteral("błend")));

        spelling.setEnabled(false);
        QVERIFY(!spelling.misspelledWordAt({0, 10}).has_value());
    }

    void userDictionaryIsShared() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        const QString path = userDictionaryPath(QStringLiteral("pl_PL"));
        QVERIFY(path.startsWith(m_home.path()));
        QCOMPARE(path, userDictionaryPath(QStringLiteral("pl")));
        SpellChecker first;
        first.setLanguages({QStringLiteral("pl")});
        QVERIFY(!first.check(QStringLiteral("qcodeeditowy")));
        first.addToDictionary(QStringLiteral("qcodeeditowy"));
        QVERIFY(first.check(QStringLiteral("qcodeeditowy")));
        SpellChecker second;
        second.setLanguages({QStringLiteral("pl")});
        QVERIFY(second.check(QStringLiteral("qcodeeditowy")));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(QString::fromUtf8(file.readAll()).split(QLatin1Char('\n')).contains(QStringLiteral("qcodeeditowy")));
        file.close();

        // A word another program added is taken in.
        QTest::qWait(20);
        QVERIFY(file.open(QIODevice::Append | QIODevice::Text));
        QTextStream(&file) << "gicioriański\n";
        file.close();
        QVERIFY(!second.check(QStringLiteral("gicioriański")));
        second.refreshUserDictionary();
        QVERIFY(second.check(QStringLiteral("gicioriański")));
    }

    // The user's own corrections are the only, sure suggestion of their word;
    // words and corrections can be removed again.
    void userCorrectionsAndRemovals() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        QVERIFY(!checker.check(QStringLiteral("Paypall")));
        checker.addCorrection(QStringLiteral("Paypall"), QStringLiteral("PayPal"));
        QCOMPARE(checker.userCorrections(),
                 (QList<QPair<QString, QString>>{{QStringLiteral("Paypall"), QStringLiteral("PayPal")}}));
        QVERIFY(QFile::exists(userCorrectionsPath(QStringLiteral("pl_PL"))));
        SpellChecker other;  // another program reads the shared file
        other.setLanguages({QStringLiteral("pl")});
        const auto suggestions = other.scoredSuggestions(QStringLiteral("Paypall"));
        QCOMPARE(suggestions.size(), 1);
        QCOMPARE(suggestions[0].text, QStringLiteral("PayPal"));
        QCOMPARE(suggestions[0].score, 0.0);
        QVERIFY(SpellChecker::isConfident(QStringLiteral("Paypall"), suggestions));
        checker.removeCorrection(QStringLiteral("Paypall"));
        QVERIFY(checker.userCorrections().isEmpty());
        QVERIFY(other.suggestions(QStringLiteral("Paypall")) != QStringList{QStringLiteral("PayPal")}
                || other.scoredSuggestions(QStringLiteral("Paypall")).value(0).score > 0);

        checker.addToDictionary(QStringLiteral("qceslowko"));
        QVERIFY(checker.check(QStringLiteral("qceslowko")));
        QVERIFY(checker.userWords().contains(QStringLiteral("qceslowko")));
        checker.removeFromDictionary(QStringLiteral("qceslowko"));
        QVERIFY(!checker.userWords().contains(QStringLiteral("qceslowko")));
        QVERIFY(!checker.check(QStringLiteral("qceslowko")));
    }

    // A list of forms with counts becomes stem counts in the cache, and common
    // words then come before rare ones ("dialac": działać, not dialach).
    void wordFrequenciesFromFormCounts() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        if (hasWordFrequencies(QStringLiteral("pl"))) QSKIP("Polish word frequencies installed system-wide");
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        // The forms of the dictionary are needed for the ranking (a few seconds).
        QTRY_VERIFY_WITH_TIMEOUT(checker.suggestionIndexReady(), 60000);
        QCOMPARE(checker.suggestions(QStringLiteral("dialac")).value(0), QStringLiteral("dialach"));

        const QString counts = m_home.filePath(QStringLiteral("pl_counts.txt"));
        QFile list(counts);
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Text));
        // rzeczy: two stems (rzecz, rzeczy); xyzzyq: unknown; dial: below 98 %.
        list.write("działać 6000\ndziałają 3000\nrzeczy 1000\nxyzzyq 500\ndial 1\n");
        list.close();
        QString error;
        QVERIFY2(buildWordFrequencies(QStringLiteral("pl_PL"), counts, &error), qPrintable(error));
        QVERIFY(hasWordFrequencies(QStringLiteral("pl")));

        QFile file(m_home.filePath(QStringLiteral("cache/siplasplas/spell/freq/pl.lemma.freq")));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines, (QStringList{QStringLiteral("# 10501 words in pl_counts.txt, 10001 known to pl_PL"),
                                     QStringLiteral("działać\t9000"), QStringLiteral("rzecz\t500"),
                                     QStringLiteral("rzeczy\t500")}));

        // Read at the next suggestions, without a restart.
        QCOMPARE(checker.suggestions(QStringLiteral("dialac")).value(0), QStringLiteral("działać"));
    }

    // A downloaded list without its stem file (e.g. a cleared cache): the
    // checker makes the file again in the background.
    void wordFrequenciesRemadeFromTheList() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        const QString stems = m_home.filePath(QStringLiteral("cache/siplasplas/spell/freq/pl.lemma.freq"));
        if (!QFile::exists(stems)) QSKIP("Needs wordFrequenciesFromFormCounts");
        QVERIFY(QFile::remove(stems));
        QVERIFY(!hasWordFrequencies(QStringLiteral("pl")));
        QCOMPARE(wordFrequencyListPath(QStringLiteral("pl")), m_home.filePath(QStringLiteral("word-frequencies/pl_50k.txt")));
        QVERIFY(QDir().mkpath(wordFrequencyListDir()));
        QFile list(wordFrequencyListPath(QStringLiteral("pl")));
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Text));
        list.write("działać 6000\ndziałają 3000\n");
        list.close();
        QVERIFY(hasWordFrequencies(QStringLiteral("pl")));

        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        QTRY_VERIFY_WITH_TIMEOUT(checker.suggestionIndexReady(), 60000);
        checker.suggestions(QStringLiteral("dialac"));  // starts making the file
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(stems), 30000);
        QTRY_COMPARE(checker.suggestions(QStringLiteral("dialac")).value(0), QStringLiteral("działać"));
    }

    // Reviewing a text: every misspelled word of a range, and replacing several
    // as one undo step, an overlapping range skipped.
    void misspelledWordsAndReplaceWords() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        // With frequencies the words of a moved space must be common.
        const QString counts = m_home.filePath(QStringLiteral("pl_review.txt"));
        QFile list(counts);
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Text));
        list.write("na 9000\noknie 3000\nkot 1000\n");
        list.close();
        QVERIFY(buildWordFrequencies(QStringLiteral("pl_PL"), counts));
        CodeEditArea area;
        auto* doc = new SimpleTextDocument(&area);
        area.setDocument(doc);
        doc->setText(QStringLiteral("Stoi n aoknie kot, tlyko teraz.\nDrugi wiersz bez blendu."));
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});

        const auto all = spelling.misspelledWords({0, 0}, {1, 100});
        QStringList words;
        for (const auto& word : all) words << word.word;
        QCOMPARE(words, (QStringList{QStringLiteral("aoknie"), QStringLiteral("tlyko"), QStringLiteral("blendu")}));
        QCOMPARE(all[2].start, (TextCursor{1, 17}));
        const auto secondLine = spelling.misspelledWords({1, 0}, {1, 100});
        QCOMPARE(secondLine.size(), 1);
        QCOMPARE(secondLine[0].word, QStringLiteral("blendu"));

        const auto phrases = spelling.phraseCorrections(all[0]);
        QVERIFY(!phrases.isEmpty());
        QCOMPARE(phrases[0].replacement, QStringLiteral("na oknie"));
        const int steps = area.undoStack()->count();
        // "aoknie" alone overlaps "n aoknie": skipped.
        spelling.replaceWords({{phrases[0].range, phrases[0].replacement},
                               {all[0], QStringLiteral("oknie")},
                               {all[1], QStringLiteral("tylko")}});
        QCOMPARE(doc->lineAt(0), QStringLiteral("Stoi na oknie kot, tylko teraz."));
        QCOMPARE(area.undoStack()->count(), steps + 1);
        area.undoStack()->undo();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Stoi n aoknie kot, tlyko teraz."));
    }

    // A word with a single close correction is corrected when a space is
    // typed after it, only when asked, not in pasted text; undo brings it
    // back. One farther off is only offered.
    void correctsWhileTyping() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        // Frequencies known for the test: "być" common, its neighbours very
        // rare, so "bhyc" (h typed between b and y) has a single suggestion.
        const QString counts = m_home.filePath(QStringLiteral("pl_typing.txt"));
        QFile list(counts);
        QVERIFY(list.open(QIODevice::WriteOnly | QIODevice::Text));
        list.write("być 9000\ni 5000\nja 3000\nagent 1000\n");
        list.close();
        QVERIFY(buildWordFrequencies(QStringLiteral("pl_PL"), counts));
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        QTRY_VERIFY_WITH_TIMEOUT(checker.suggestionIndexReady(), 60000);
        const auto bhyc = checker.scoredSuggestions(QStringLiteral("bhyc"));
        QCOMPARE(checker.suggestions(QStringLiteral("bhyc")), QStringList{QStringLiteral("być")});
        QVERIFY(SpellChecker::isConfident(QStringLiteral("bhyc"), bhyc));
        // Shift held a letter too long: the word's own case, a small cost.
        const auto shift = checker.scoredSuggestions(QStringLiteral("BHyc"));
        QCOMPARE(shift.value(0).text, QStringLiteral("Być"));
        QVERIFY(shift.value(0).score > bhyc.value(0).score);
        // A letter too many that touches neither neighbour: offered, not sure.
        const auto agents = checker.scoredSuggestions(QStringLiteral("agents"));
        QCOMPARE(agents.size(), 1);
        QVERIFY(!SpellChecker::isConfident(QStringLiteral("agents"), agents));

        CodeEditArea area;
        auto* doc = new SimpleTextDocument(&area);
        area.setDocument(doc);
        area.resize(400, 100);
        area.show();
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        QSignalSpy corrected(&spelling, &CodeEditSpelling::corrected);
        QVERIFY(!spelling.correctsWhileTyping());
        QTest::keyClicks(&area, QStringLiteral("Ja bhyc "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc "));

        spelling.setCorrectWhileTyping(true);
        QTest::keyClicks(&area, QStringLiteral("i bhyc "));
        QTRY_COMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc i być "));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 14}));
        QCOMPARE(corrected.size(), 1);
        QCOMPARE(corrected.at(0).at(0).toString(), QStringLiteral("bhyc"));
        QCOMPARE(corrected.at(0).at(1).toString(), QStringLiteral("być"));
        // The correction is its own step: the typing stays.
        area.undoStack()->undo();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc i bhyc "));
        area.undoStack()->redo();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc i być "));

        area.insertText(QStringLiteral("bhyc "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc i być bhyc "));
        // Not sure enough: left as typed.
        QTest::keyClicks(&area, QStringLiteral("agents "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja bhyc i być bhyc agents "));
        QCOMPARE(corrected.size(), 1);
    }

    // As the area's child (no parent given) the spelling is deleted after the
    // area's own part; it must not touch the area then. Needs no dictionary.
    void deletedWithItsArea() {
        auto* area = new CodeEditArea;
        auto* doc = new SimpleTextDocument(area);
        area->setDocument(doc);
        auto* spelling = new CodeEditSpelling(area);
        QCOMPARE(spelling->parent(), area);
        doc->setText(QStringLiteral("Ths is a txt"));
        area->resize(300, 100);
        area->show();
        spelling->recheck();
        delete area;
    }
};

QTEST_MAIN(TestSpell)
#include "test_spell.moc"
