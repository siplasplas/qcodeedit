#include <qce/spell/SpellChecker.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <vector>

using namespace qce::spell;

// Real typing mistakes: texts typed again by hand (tests/data/spell, see its
// README) against the published ones. The mistakes are found and corrected
// at least as well as when this test was written; a sure correction is never
// wrong. Uses the system's en_US, German, Spanish and French dictionaries
// (skips without them), without word frequencies, in a temporary cache.
class TestSpellTexts : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;

    struct Result {
        int pairs = 0;      // words typed differently
        int flagged = 0;    // of them, found misspelled
        int first = 0;      // the right word suggested first
        int shown = 0;      // the right word among the suggestions
        int sure = 0;       // a sure correction (isConfident())
        QStringList sureWrong;
    };

    static QStringList wordsOf(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        const QString text = QString::fromUtf8(file.readAll());
        QStringList words;
        for (const auto& [start, length] : checkableWords(text, 0, int(text.size())))
            words.append(text.mid(start, length));
        return words;
    }

    // Both kinds of apostrophe as one (typing ' for ’ is no mistake), and
    // either case: a suggestion keeps the typed word's capital ("Uubntu" →
    // Ubuntu for "ubuntu").
    static QString plain(QString word) { return word.replace(QChar(0x2019), QLatin1Char('\'')).toLower(); }

    // The words of the typed text paired with those of the published one
    // where they differ: runs of the same length between the words both
    // have (their longest common subsequence).
    static QList<QPair<QString, QString>> typingMistakes(const QStringList& published, const QStringList& typed) {
        const int n = int(published.size()), m = int(typed.size());
        std::vector<std::vector<int>> common(size_t(n + 1), std::vector<int>(size_t(m + 1), 0));
        for (int i = n - 1; i >= 0; --i)
            for (int j = m - 1; j >= 0; --j)
                common[i][j] = plain(published[i]) == plain(typed[j]) ? common[i + 1][j + 1] + 1
                                                                      : std::max(common[i + 1][j], common[i][j + 1]);
        QList<QPair<QString, QString>> pairs;
        int i = 0, j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && plain(published[i]) == plain(typed[j])) {
                ++i;
                ++j;
                continue;
            }
            // A run of differences up to the next word both have.
            const int fromI = i, fromJ = j;
            while (i < n && j < m && plain(published[i]) != plain(typed[j])) {
                if (common[i + 1][j] >= common[i][j + 1]) ++i;
                else ++j;
            }
            if (i == n || j == m) i = n, j = m;
            if (i - fromI == j - fromJ)
                for (int k = 0; k < i - fromI; ++k) pairs.append({typed[fromJ + k], published[fromI + k]});
        }
        return pairs;
    }

    Result measure(const QString& language, const QString& name) {
        const QString dir = QStringLiteral(QCE_TEST_DATA_DIR "/spell/");
        const QStringList published = wordsOf(dir + name + QStringLiteral(".orig.txt"));
        const QStringList typed = wordsOf(dir + name + QStringLiteral(".typed.txt"));
        SpellChecker checker;
        checker.setLanguages({language});
        Result result;
        if (!QTest::qWaitFor([&] { return checker.suggestionIndexReady(); }, 60000)) return result;
        for (const auto& [word, right] : typingMistakes(published, typed)) {
            ++result.pairs;
            if (checker.check(word)) continue;  // another real word ("in" for "on")
            ++result.flagged;
            const auto suggestions = checker.scoredSuggestions(word);
            const bool first = !suggestions.isEmpty() && plain(suggestions.front().text) == plain(right);
            result.first += first;
            for (const auto& suggestion : suggestions) result.shown += plain(suggestion.text) == plain(right);
            if (SpellChecker::isConfident(word, suggestions)) {
                ++result.sure;
                if (!first) result.sureWrong << word + QStringLiteral(" → ") + suggestions.front().text;
            }
        }
        for (const QString& wrong : std::as_const(result.sureWrong)) qInfo("sure but wrong: %s", qPrintable(wrong));
        qInfo("%s: %d typed differently, %d found, %d right first, %d shown, %d sure", qPrintable(name),
              result.pairs, result.flagged, result.first, result.shown, result.sure);
        return result;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_home.isValid());
        qputenv("XDG_DATA_HOME", m_home.filePath(QStringLiteral("data")).toLocal8Bit());
        qputenv("XDG_CACHE_HOME", m_home.filePath(QStringLiteral("cache")).toLocal8Bit());
    }

    void englishTypedFast() {
        if (dictionaryFor(QStringLiteral("en_US")).isEmpty()) QSKIP("No English dictionary (hunspell-en-us)");
        const Result r = measure(QStringLiteral("en_US"), QStringLiteral("01.en"));
        QCOMPARE(r.sureWrong, QStringList());
        // Measured: 38 of 49 found first, 44 shown (no frequencies here).
        QVERIFY(r.flagged >= 49);
        QVERIFY(r.first >= 38);
        QVERIFY(r.shown >= 44);
    }

    void germanWithoutUmlauts() {
        if (dictionaryFor(QStringLiteral("de_DE")).isEmpty()) QSKIP("No German dictionary (hunspell-de-de)");
        const Result r = measure(QStringLiteral("de_DE"), QStringLiteral("01.de"));
        QCOMPARE(r.sureWrong, QStringList());
        // Measured: 46 of 61 found first, 49 shown (44 and 48 before compound
        // words were asked of hunspell too: "Betriebsystem" → Betriebssystem).
        QVERIFY(r.flagged >= 61);
        QVERIFY(r.first >= 46);
        QVERIFY(r.shown >= 49);
    }

    void spanishWithoutAccents() {
        if (dictionaryFor(QStringLiteral("es_ES")).isEmpty()) QSKIP("No Spanish dictionary (hunspell-es)");
        const Result r = measure(QStringLiteral("es_ES"), QStringLiteral("01.es"));
        QCOMPARE(r.sureWrong, QStringList());
        // Measured: 46 of 56 found first, 50 shown.
        QVERIFY(r.flagged >= 56);
        QVERIFY(r.first >= 46);
        QVERIFY(r.shown >= 50);
    }

    void frenchWithoutAccents() {
        if (dictionaryFor(QStringLiteral("fr")).isEmpty()) QSKIP("No French dictionary (hunspell-fr)");
        const Result r = measure(QStringLiteral("fr"), QStringLiteral("01.fr"));
        QCOMPARE(r.sureWrong, QStringList());
        // Measured: 51 of 58 found first, 55 shown (hunspell alone: 47 first).
        QVERIFY(r.flagged >= 58);
        QVERIFY(r.first >= 51);
        QVERIFY(r.shown >= 55);
    }
};

QTEST_MAIN(TestSpellTexts)
#include "test_spell_texts.moc"
