#include <qce/spell/CodeEditSpelling.h>
#include <qce/spell/GrammarReview.h>
#include <qce/spell/SpellChecker.h>

#include <qce/CodeEditArea.h>
#include <qce/SimpleTextDocument.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

#include "KateTestPaths.h"

#ifdef QCE_TEST_GRAMMAR
#include <qce/grammar/Morphology.h>
#endif

#include <QDir>
#include <QPushButton>
#include <QTreeWidget>
#include <QRegularExpression>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUndoStack>
#include <QtTest>

#include <sqlite3.h>

using namespace qce;
using namespace qce::spell;

namespace {

// A stand-in grammar for Polish: "ze" before any word is a mistake, mended
// by "że" (a real checker knows which words take "ze").
class FakeGrammar : public GrammarChecker {
public:
    QString language() const override { return QStringLiteral("pl"); }
    QList<GrammarIssue> check(const QString& text) const override {
        QList<GrammarIssue> issues;
        static const QRegularExpression pattern(QStringLiteral("\\bze (\\w+)"));
        for (auto it = pattern.globalMatch(text); it.hasNext();) {
            const auto match = it.next();
            GrammarIssue issue;
            issue.start = int(match.capturedStart());
            issue.end = int(match.capturedEnd());
            issue.rule = QStringLiteral("government");
            issue.replacements.append({issue.start, issue.start + 2, QStringLiteral("że")});
            issues.append(issue);
        }
        return issues;
    }
};

} // namespace

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
        // A user dictionary of an earlier version, to be taken in.
        QVERIFY(QDir().mkpath(m_home.filePath(QStringLiteral("siplasplas/spell"))));
        QFile old(m_home.filePath(QStringLiteral("siplasplas/spell/pl.dic")));
        QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Text));
        old.write("staryslowniczek\n");
        old.close();
        // A database of 1.46/1.47 (rowid tables, whole paths), to be converted.
        sqlite3* database = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(m_home.filePath(QStringLiteral("siplasplas/spell/spelling.sqlite")))
                                  .constData(),
                              &database),
                 SQLITE_OK);
        const QByteArray oldFile = QFile::encodeName(m_home.filePath(QStringLiteral("old/x.txt")));
        const QByteArray sql =
            "CREATE TABLE words(language TEXT NOT NULL, word TEXT NOT NULL, PRIMARY KEY(language, word));"
            "CREATE TABLE corrections(language TEXT NOT NULL, typed TEXT NOT NULL, correction TEXT NOT NULL,"
            " PRIMARY KEY(language, typed));"
            "CREATE TABLE ignored(path TEXT NOT NULL, word TEXT NOT NULL, PRIMARY KEY(path, word));"
            "INSERT INTO words VALUES('pl', 'starabazowy');"
            "INSERT INTO corrections VALUES('pl', 'Starabaza', 'StaraBaza');"
            "INSERT INTO ignored VALUES('" + oldFile + "', 'qwzxstare');";
        QCOMPARE(sqlite3_exec(database, sql.constData(), nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(database);
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

    // A character AltGr types on a letter key (© on r, ” on b of the Polish
    // keyboard) after a letter: AltGr not let go when a part around it is
    // wrong and the word is right with it as its key's letter or without it;
    // otherwise the sign itself, between words.
    void altGrMarksInWords() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        const QString before = keyboardLayout();
        QVERIFY(setKeyboardLayout(QStringLiteral("pl")));
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        const QString text = QStringLiteral("Mam któ©re i któ©ych, zró” to, są” i „zrób”, Firma© ©");
        QStringList words;
        for (const auto& word : checker.checkableWords(text, 0, int(text.size())))
            words << text.mid(word.first, word.second);
        QCOMPARE(words, (QStringList{QStringLiteral("Mam"), QStringLiteral("któ©re"), QStringLiteral("któ©ych"),
                                     QStringLiteral("zró”"), QStringLiteral("to"), QStringLiteral("są"),
                                     QStringLiteral("zrób"), QStringLiteral("Firma")}));
        setKeyboardLayout(before);
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

    void grammarOnRightWordsOnly() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Wiem, ze jest dobrze.\nWiem, ze jestt dobrze."));
        CodeEditArea area;
        area.setDocument(&doc);
        area.resize(600, 200);
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        spelling.recheck();
        QVERIFY(area.extraSelections(CodeEditArea::GrammarLayer).isEmpty());  // no checker yet

        spelling.setGrammarChecker(std::make_shared<FakeGrammar>());
        spelling.recheck();
        // The second line has a misspelled word: spelling first.
        const auto grammar = area.extraSelections(CodeEditArea::GrammarLayer);
        QCOMPARE(grammar.size(), 1);
        QCOMPARE(grammar.front().start, (TextCursor{0, 6}));
        QCOMPARE(grammar.front().end, (TextCursor{0, 13}));
        QCOMPARE(grammar.front().underline, spelling.grammarUnderlineColor());
        QCOMPARE(area.extraSelections(CodeEditArea::SpellingLayer).size(), 1);

        QVERIFY(!spelling.grammarMistakeAt({0, 2}).has_value());
        QVERIFY(!spelling.grammarMistakeAt({1, 7}).has_value());
        const auto mistake = spelling.grammarMistakeAt({0, 7});
        QVERIFY(mistake.has_value());
        QCOMPARE(mistake->issue.rule, QStringLiteral("government"));
        QCOMPARE(mistake->corrections.size(), 1);
        QCOMPARE(mistake->corrections.front().first.word, QStringLiteral("ze"));
        spelling.replaceWord(mistake->corrections.front().first, mistake->corrections.front().second);
        QCOMPARE(doc.lineAt(0), QStringLiteral("Wiem, że jest dobrze."));
        spelling.recheck();
        QVERIFY(area.extraSelections(CodeEditArea::GrammarLayer).isEmpty());

        // Only for its language; and gone with the checker.
        doc.setText(QStringLiteral("Wiem, ze jest dobrze."));
        if (installed(QStringLiteral("en"))) {
            spelling.setLanguages({QStringLiteral("en")});
            spelling.recheck();
            QVERIFY(area.extraSelections(CodeEditArea::GrammarLayer).isEmpty());
            spelling.setLanguages({QStringLiteral("pl")});
        }
        spelling.recheck();
        QCOMPARE(area.extraSelections(CodeEditArea::GrammarLayer).size(), 1);
        spelling.setGrammarChecker(nullptr);
        QVERIFY(area.extraSelections(CodeEditArea::GrammarLayer).isEmpty());
    }

    void grammarReview() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Wiem, ze jest dobrze.\nWiem, ze jestt.\nMyślę, ze tak."));
        CodeEditArea area;
        area.setDocument(&doc);
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        spelling.setGrammarChecker(std::make_shared<FakeGrammar>());
        const auto all = spelling.grammarMistakes({0, 0}, {2, 14});
        QCOMPARE(all.size(), 2);  // not the line with a misspelled word
        QCOMPARE(all[1].start, (TextCursor{2, 7}));

        GrammarReviewDialog review(spelling, 0, 2, false);
        QCOMPARE(review.mistakeCount(), 2);
        auto* list = review.findChild<QTreeWidget*>();
        QVERIFY(list);
        list->topLevelItem(1)->setCheckState(0, Qt::Checked);
        QPushButton* correct = nullptr;
        for (auto* button : review.findChildren<QPushButton*>())
            if (button->text() == GrammarReviewDialog::tr("&Correct")) correct = button;
        QVERIFY(correct);
        correct->click();
        QCOMPARE(doc.lineAt(2), QStringLiteral("Myślę, że tak."));
        QCOMPARE(review.correctedSoFar(), 1);
        QCOMPARE(review.mistakeCount(), 1);  // looked for again
        area.undoStack()->undo();
        QCOMPARE(doc.lineAt(2), QStringLiteral("Myślę, ze tak."));
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
        QVERIFY(userDataPath().startsWith(m_home.path()));
        // A user dictionary of an earlier version is taken in and moved away.
        SpellChecker first;
        first.setLanguages({QStringLiteral("pl")});
        QVERIFY(first.check(QStringLiteral("staryslowniczek")));
        QVERIFY(!QFile::exists(m_home.filePath(QStringLiteral("siplasplas/spell/pl.dic"))));
        QVERIFY(QFile::exists(m_home.filePath(QStringLiteral("siplasplas/spell/imported/pl.dic"))));
        // So is a database of 1.46/1.47.
        QVERIFY(first.check(QStringLiteral("starabazowy")));
        QCOMPARE(first.suggestions(QStringLiteral("Starabaza")), QStringList{QStringLiteral("StaraBaza")});
        first.removeCorrection(QStringLiteral("Starabaza"));  // the corrections test expects its own only
        SpellChecker oldFile;
        oldFile.setLanguages({QStringLiteral("pl")});
        oldFile.setDocumentPath(m_home.filePath(QStringLiteral("old/x.txt")));
        QVERIFY(oldFile.check(QStringLiteral("qwzxstare")));

        QVERIFY(!first.check(QStringLiteral("qcodeeditowy")));
        first.addToDictionary(QStringLiteral("qcodeeditowy"));
        QVERIFY(first.check(QStringLiteral("qcodeeditowy")));
        SpellChecker second;
        second.setLanguages({QStringLiteral("pl")});
        QVERIFY(second.check(QStringLiteral("qcodeeditowy")));
        QVERIFY(second.userWords().contains(QStringLiteral("qcodeeditowy")));

        // A word another program added is taken in.
        sqlite3* other = nullptr;
        QCOMPARE(sqlite3_open(QFile::encodeName(userDataPath()).constData(), &other), SQLITE_OK);
        sqlite3_busy_timeout(other, 3000);
        QCOMPARE(sqlite3_exec(other, "INSERT INTO words(language, word) VALUES('pl', 'gicioriański')", nullptr,
                              nullptr, nullptr),
                 SQLITE_OK);
        sqlite3_close(other);
        QVERIFY(!second.check(QStringLiteral("gicioriański")));
        second.refreshUserDictionary();
        QVERIFY(second.check(QStringLiteral("gicioriański")));
    }

    // Words ignored in a file are kept for it, and follow it when it is moved
    // or copied (alone or with its directory).
    void ignoredWordsKeptPerFile() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        const QString dir = m_home.filePath(QStringLiteral("docs"));
        const QString file = dir + QStringLiteral("/a.txt");
        const QString word = QStringLiteral("qwzxignorowane");
        {
            SpellChecker checker;
            checker.setLanguages({QStringLiteral("pl")});
            checker.setDocumentPath(file);
            QVERIFY(!checker.check(word));
            checker.ignore(word);
            QVERIFY(checker.check(word));
        }
        const auto ignoredIn = [&](const QString& path) {
            SpellChecker checker;
            checker.setLanguages({QStringLiteral("pl")});
            checker.setDocumentPath(path);
            return checker.check(word);
        };
        QVERIFY(ignoredIn(file));                                  // after the window closed
        QVERIFY(!ignoredIn(dir + QStringLiteral("/other.txt")));   // another file: not
        QVERIFY(!ignoredIn(QString()));                            // no file: not

        moveSpellingData(file, dir + QStringLiteral("/b.txt"));
        QVERIFY(!ignoredIn(file));
        QVERIFY(ignoredIn(dir + QStringLiteral("/b.txt")));
        moveSpellingData(dir, m_home.filePath(QStringLiteral("moved")));
        QVERIFY(ignoredIn(m_home.filePath(QStringLiteral("moved/b.txt"))));
        copySpellingData(m_home.filePath(QStringLiteral("moved")), m_home.filePath(QStringLiteral("copy")));
        QVERIFY(ignoredIn(m_home.filePath(QStringLiteral("copy/b.txt"))));
        QVERIFY(ignoredIn(m_home.filePath(QStringLiteral("moved/b.txt"))));
        removeSpellingData(m_home.filePath(QStringLiteral("copy/b.txt")));
        QVERIFY(!ignoredIn(m_home.filePath(QStringLiteral("copy/b.txt"))));

        // A directory is its own paths only: "moved2" begins with "moved" too.
        copySpellingData(m_home.filePath(QStringLiteral("moved/b.txt")), m_home.filePath(QStringLiteral("moved2/b.txt")));
        removeSpellingData(m_home.filePath(QStringLiteral("moved2/")));
        QVERIFY(!ignoredIn(m_home.filePath(QStringLiteral("moved2/b.txt"))));
        QVERIFY(ignoredIn(m_home.filePath(QStringLiteral("moved/b.txt"))));
        // Moved into a directory where the file already has ignored words:
        // the words of both are kept.
        {
            SpellChecker other;
            other.setLanguages({QStringLiteral("pl")});
            other.setDocumentPath(m_home.filePath(QStringLiteral("target/b.txt")));
            other.ignore(QStringLiteral("qwzxdrugie"));
        }
        copySpellingData(m_home.filePath(QStringLiteral("moved")), m_home.filePath(QStringLiteral("source")));
        moveSpellingData(m_home.filePath(QStringLiteral("source")), m_home.filePath(QStringLiteral("target")));
        QVERIFY(!ignoredIn(m_home.filePath(QStringLiteral("source/b.txt"))));
        QVERIFY(ignoredIn(m_home.filePath(QStringLiteral("target/b.txt"))));
        {
            SpellChecker merged;
            merged.setLanguages({QStringLiteral("pl")});
            merged.setDocumentPath(m_home.filePath(QStringLiteral("target/b.txt")));
            QVERIFY(merged.check(QStringLiteral("qwzxdrugie")));
        }

        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        checker.setDocumentPath(m_home.filePath(QStringLiteral("moved/b.txt")));
        QCOMPARE(checker.ignoredWords(), QStringList{word});
        checker.unignore(word);
        QVERIFY(!checker.check(word));
        QVERIFY(!ignoredIn(m_home.filePath(QStringLiteral("moved/b.txt"))));
    }

    // Hunspell rejects an added word whose lower case belongs to a
    // dictionary word in mixed case ("Kazaa", the dictionary has "KaZaA");
    // the user's words count anyway.
    void addedWordLikeMixedCaseOne() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        SpellChecker checker;
        checker.setLanguages({QStringLiteral("pl")});
        if (!checker.check(QStringLiteral("KaZaA"))) QSKIP("The Polish dictionary has no KaZaA");
        QVERIFY(!checker.check(QStringLiteral("Kazaa")));
        checker.addToDictionary(QStringLiteral("Kazaa"));
        QVERIFY(checker.check(QStringLiteral("Kazaa")));
        QVERIFY(checker.check(QStringLiteral("KAZAA")));
        QVERIFY(!checker.check(QStringLiteral("kazaa")));  // added with a capital
        checker.removeFromDictionary(QStringLiteral("Kazaa"));
        QVERIFY(!checker.check(QStringLiteral("Kazaa")));
        // Added in lower case: also with a capital, and the dictionary's own.
        checker.addToDictionary(QStringLiteral("kazaa"));
        QVERIFY(checker.check(QStringLiteral("kazaa")));
        QVERIFY(checker.check(QStringLiteral("Kazaa")));
        QVERIFY(checker.check(QStringLiteral("KaZaA")));
        checker.removeFromDictionary(QStringLiteral("kazaa"));
        QVERIFY(!checker.check(QStringLiteral("Kazaa")));
        // Lower case after a capital replaces it; a capital after lower case
        // adds nothing.
        checker.addToDictionary(QStringLiteral("Kazaa"));
        checker.addToDictionary(QStringLiteral("kazaa"));
        QVERIFY(checker.userWords().contains(QStringLiteral("kazaa")));
        QVERIFY(!checker.userWords().contains(QStringLiteral("Kazaa")));
        checker.addToDictionary(QStringLiteral("Kazaa"));
        QVERIFY(!checker.userWords().contains(QStringLiteral("Kazaa")));
        checker.removeFromDictionary(QStringLiteral("kazaa"));
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
        QVERIFY(QFile::exists(userDataPath()));
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
        QCOMPARE(lines, (QStringList{QStringLiteral("# v2 10501 words in pl_counts.txt, 10001 known to pl_PL"),
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
        // An h touching both neighbours (0.7) and ć without its diacritic
        // (0.35), as fitted to real typing mistakes: offered alone, not sure.
        QVERIFY(!SpellChecker::isConfident(QStringLiteral("bhyc"), bhyc));
        // A missing diacritic alone is sure.
        QVERIFY(SpellChecker::isConfident(QStringLiteral("byc"), checker.scoredSuggestions(QStringLiteral("byc"))));
        // Shift held a letter too long: the word's own case, a small cost.
        const auto shift = checker.scoredSuggestions(QStringLiteral("BHyc"));
        QCOMPARE(shift.value(0).text, QStringLiteral("Być"));
        QVERIFY(shift.value(0).score > bhyc.value(0).score);
        // Shift and AltGr held together are one slip: no more than Shift alone.
        const auto held = checker.scoredSuggestions(QStringLiteral("ÞÓŁączyłęm"));
        QCOMPARE(held.value(0).text, QStringLiteral("Połączyłem"));
        QCOMPARE(held.value(0).score, checker.scoredSuggestions(QStringLiteral("POŁączyłem")).value(0).score);
        // A capital after a small letter is camelCase rather: much more.
        QVERIFY(checker.scoredSuggestions(QStringLiteral("bhYc")).value(0).score
                >= bhyc.value(0).score + 0.9);
        // A letter too many that touches neither neighbour: offered, not sure.
        // (Also "agenta": s for a, the key beside it.)
        const auto agents = checker.scoredSuggestions(QStringLiteral("agents"));
        QVERIFY(checker.suggestions(QStringLiteral("agents")).contains(QStringLiteral("agent")));
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
        QTest::keyClicks(&area, QStringLiteral("Ja byc "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja byc "));

        spelling.setCorrectWhileTyping(true);
        QTest::keyClicks(&area, QStringLiteral("i byc "));
        QTRY_COMPARE(doc->lineAt(0), QStringLiteral("Ja byc i być "));
        QCOMPARE(area.cursorPosition(), (TextCursor{0, 13}));
        QCOMPARE(corrected.size(), 1);
        QCOMPARE(corrected.at(0).at(0).toString(), QStringLiteral("byc"));
        QCOMPARE(corrected.at(0).at(1).toString(), QStringLiteral("być"));
        // The correction is its own step: the typing stays.
        area.undoStack()->undo();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja byc i byc "));
        area.undoStack()->redo();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja byc i być "));

        area.insertText(QStringLiteral("byc "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja byc i być byc "));
        // Not sure enough: left as typed.
        QTest::keyClicks(&area, QStringLiteral("agents "));
        QCoreApplication::processEvents();
        QCOMPARE(doc->lineAt(0), QStringLiteral("Ja byc i być byc agents "));
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

#ifdef QCE_TEST_GRAMMAR
    // The built-in grammar comes by itself with its data files (last: it
    // leaves no files behind for the tests above).
    void builtinGrammar() {
        if (!installed(QStringLiteral("pl"))) QSKIP("No Polish dictionary (hunspell-pl)");
        QVERIFY(!builtinGrammarChecker(QStringLiteral("pl")));  // no files yet
        QDir dir(grammarDataDir());
        QVERIFY(dir.mkpath(QStringLiteral(".")));
        qce::grammar::MorphologyBuilder builder;
        QVERIFY(builder.add("ze", "z", "prep:gen:wok"));
        QVERIFY(builder.add("ze", "z", "prep:inst:wok"));
        QVERIFY(builder.add("że", "że", "comp"));
        QVERIFY(builder.add("wiem", "wiedzieć", "fin:sg:pri:imperf"));
        QVERIFY(builder.add("jest", "być", "fin:sg:ter:imperf"));
        QVERIFY(builder.add("dobrze", "dobrze", "adv:pos"));
        std::string error;
        QVERIFY(builder.write(QFile::encodeName(dir.filePath(QStringLiteral("pl.morph"))).toStdString(), nullptr, &error));
        QVERIFY(QFile::copy(QStringLiteral(QCE_GRAMMAR_SOURCE_DATA_DIR "/pl.grammar"), dir.filePath(QStringLiteral("pl.grammar"))));

        SimpleTextDocument doc;
        doc.setText(QStringLiteral("Wiem, ze jest dobrze."));
        CodeEditArea area;
        area.setDocument(&doc);
        area.resize(600, 200);
        CodeEditSpelling spelling(&area);
        spelling.setLanguages({QStringLiteral("pl")});
        QVERIFY(spelling.grammarChecker());
        spelling.recheck();
        const auto grammar = area.extraSelections(CodeEditArea::GrammarLayer);
        QCOMPARE(grammar.size(), 1);
        QCOMPARE(grammar.front().start, (TextCursor{0, 6}));
        QCOMPARE(grammar.front().end, (TextCursor{0, 13}));
        const auto mistake = spelling.grammarMistakeAt({0, 7});
        QVERIFY(mistake.has_value());
        QCOMPARE(mistake->issue.rule, QStringLiteral("government"));
        QVERIFY(!mistake->issue.message.isEmpty());
        QCOMPARE(mistake->corrections.size(), 1);
        QCOMPARE(mistake->corrections.front().first.word, QStringLiteral("ze"));
        QCOMPARE(mistake->corrections.front().second, QStringLiteral("że"));

        // Off, and back on.
        spelling.setGrammarEnabled(false);
        QVERIFY(area.extraSelections(CodeEditArea::GrammarLayer).isEmpty());
        spelling.setGrammarEnabled(true);
        spelling.recheck();
        QCOMPARE(area.extraSelections(CodeEditArea::GrammarLayer).size(), 1);
        dir.removeRecursively();
    }
#endif
};

QTEST_MAIN(TestSpell)
#include "test_spell.moc"
