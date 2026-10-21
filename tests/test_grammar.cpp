// qcodeedit-grammar: the morphology file and the grammar rules, on small
// dictionaries in PoliMorf's columns and the Polish rules file.

#include <qce/grammar/Grammar.h>
#include <qce/grammar/Morphology.h>

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <sstream>

using namespace qce::grammar;

namespace {

const char* const kPoliMorf =
    "#!DICT-ID test\n"
    "#<COPYRIGHT>\n"
    "# not an entry\n"
    "#</COPYRIGHT>\n"
    "mam\tmama\tsubst:pl:gen:f\tnazwa_pospolita\t\n"
    "mam\tmieć\tfin:sg:pri:imperf\t\t\n"
    "mam\tmamić\timpt:sg:sec:imperf\t\t\n"
    "mama\tmama\tsubst:sg:nom:f\tnazwa_pospolita\t\n"
    "najładniejszego\tładny\tadj:sg:gen:m1.m2.m3.n:sup\t\t\n"
    "piękną\tpiękny\tadj:sg:acc:f:pos\t\t\n"
    "piękną\tpiękny\tadj:sg:inst:f:pos\t\t\n"
    "piękną\tpiękny\tadj:sg:inst:f:pos\t\t\n"  // a line twice
    "dziewczynie\tdziewczyna\tsubst:sg:dat.loc:f\tnazwa_pospolita\t\n"
    "dziewczynie\tdziewczyni\tadj:sg:nom.voc:n:pos\t\tdaw.\n"
    "Ab\tKsięga_Abdiasza\tbrev:npun\ttytuł\t\n"
    "szybko\tszybki\tadv:pos\t\t\n";

const char* const kGrammarDictionary =
    "ze\tz\tprep:gen:wok\t\t\n"
    "ze\tz\tprep:inst:wok\t\t\n"
    "ze\tz\tpart:wok\t\t\n"
    "że\tże\tcomp\t\t\n"
    "że\tże\tpart\t\t\n"
    "wiem\twiedzieć\tfin:sg:pri:imperf\t\t\n"
    "jest\tbyć\tfin:sg:ter:imperf\t\t\n"
    "jest\tjesta\tsubst:sg:inst:f\t\tdaw.\n"  // made up: an archaic reading that would fit "ze"
    "mną\tja\tppron12:sg:inst:m1.m2.m3.f.n:pri\t\t\n"
    "zbyt\tzbyt\tadv\t\t\n"
    "teraz\tteraz\tadv\t\t\n"
    "dużym\tduży\tadj:sg:inst:m1.m2.m3.n:pos\t\t\n"
    "domem\tdom\tsubst:sg:inst:m3\tnazwa_pospolita\t\n"
    "piękna\tpiękny\tadj:sg:nom.voc:f:pos\t\t\n"
    "piękną\tpiękny\tadj:sg:acc:f:pos\t\t\n"
    "piękną\tpiękny\tadj:sg:inst:f:pos\t\t\n"
    "dziewczynę\tdziewczyna\tsubst:sg:acc:f\tnazwa_pospolita\t\n"
    "sukienkę\tsukienka\tsubst:sg:acc:f\tnazwa_pospolita\t\n"
    "czerwoną\tczerwony\tadj:sg:acc:f:pos\t\t\n"
    "czerwoną\tczerwony\tadj:sg:inst:f:pos\t\t\n"
    "siostry\tsiostra\tsubst:sg:gen:f\tnazwa_pospolita\t\n"
    "siostry\tsiostra\tsubst:pl:nom.acc.voc:f\tnazwa_pospolita\t\n"
    "dziewczynie\tdziewczyna\tsubst:sg:dat.loc:f\tnazwa_pospolita\t\n"
    "dziewczynie\tdziewczyni\tadj:sg:nom.voc:n:pos\t\tdaw.\n"
    "wdzięczny\twdzięczny\tadj:sg:nom.voc:m1.m2.m3:pos\t\t\n"
    "ojcu\tojciec\tsubst:sg:dat:m1\tnazwa_pospolita\t\n"
    "mam\tmieć\tfin:sg:pri:imperf\t\t\n"
    "mam\tmama\tsubst:pl:gen:f\tnazwa_pospolita\t\n"
    "mama\tmama\tsubst:sg:nom:f\tnazwa_pospolita\t\n"
    "dzwoniła\tdzwonić\tpraet:sg:f:imperf\t\t\n"
    "po\tpo\tprep:loc\t\t\n"
    "jechałem\tjechać\tpraet:sg:m1.m2.m3:pri:imperf\t\t\n"
    "pojechałem\tpojechać\tpraet:sg:m1.m2.m3:pri:perf\t\t\n"
    "z\tz\tprep:gen:nwok\t\t\n"
    "z\tz\tprep:inst:nwok\t\t\n"
    "ż\tż\tpart\t\t\n"
    "pielgrzymka\tpielgrzymka\tsubst:sg:nom:f\tnazwa_pospolita\t\n"
    "pielgrzymką\tpielgrzymka\tsubst:sg:inst:f\tnazwa_pospolita\t\n"
    "za\tza\tprep:acc\t\t\n"
    "naśladowanie\tnaśladowanie\tsubst:sg:nom.acc:n\tnazwa_pospolita\t\n"
    "naśladowaniem\tnaśladowanie\tsubst:sg:inst:n\tnazwa_pospolita\t\n"
    "w\tw\tprep:acc:nwok\t\t\n"
    "w\tw\tprep:loc:nwok\t\t\n"
    "przeciwna\tprzeciwny\tadj:sg:nom.voc:f:pos\t\t\n"
    "przeciwną\tprzeciwny\tadj:sg:acc:f:pos\t\t\n"
    "przeciwną\tprzeciwny\tadj:sg:inst:f:pos\t\t\n"
    "stroną\tstrona\tsubst:sg:inst:f\tnazwa_pospolita\t\n"
    "stronę\tstrona\tsubst:sg:acc:f\tnazwa_pospolita\t\n"
    "strona\tstrona\tsubst:sg:nom:f\tnazwa_pospolita\t\n"
    "juz\tjuz\tsubst:sg:nom.acc:m3\tnazwa_pospolita\ttechn.|hist.\n"
    "już\tjuż\tpart\t\t\n";

bool has(const std::vector<Reading>& readings, const std::string& lemma, std::string_view tag,
         std::string_view label = {}, std::string_view qualifiers = {}) {
    return std::any_of(readings.begin(), readings.end(), [&](const Reading& r) {
        return r.lemma == lemma && r.tag == tag && r.label == label && r.qualifiers == qualifiers;
    });
}

std::vector<Finding> check(const Grammar& grammar, const std::string& text) {
    return grammar.check(tokenize(text));
}

bool suggests(const std::vector<Finding>& findings, const std::string& rule, size_t token, const std::string& word) {
    for (const Finding& f : findings)
        if (f.rule == rule)
            for (const Suggestion& s : f.suggestions)
                if (s.token == token && s.word == word) return true;
    return false;
}

} // namespace

class TestGrammar : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // A file of the given bytes in the test's directory.
    std::string file(const char* name, const std::string& bytes) {
        const QString path = m_dir.filePath(QString::fromUtf8(name));
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) out.write(bytes.data(), qint64(bytes.size()));
        return path.toStdString();
    }

    // The morphology file of tab-separated lines (form, lemma, tag, label, qualifiers).
    std::string build(const char* name, const char* lines, MorphologyStats* stats = nullptr) {
        MorphologyBuilder builder;
        std::istringstream in(lines);
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> columns(5);
            std::istringstream fields(line);
            for (std::string& column : columns) std::getline(fields, column, '\t');
            builder.add(columns[0], columns[1], columns[2], columns[3], columns[4]);
        }
        const std::string path = file(name, "");
        std::string error;
        if (!builder.write(path, stats, &error)) qWarning("%s", error.c_str());
        return path;
    }

private slots:
    void morphology() {
        MorphologyStats stats;
        const std::string path = build("test.morph", kPoliMorf, &stats);
        QCOMPARE(stats.entries, 12);
        QCOMPARE(stats.readings, 11);
        Morphology morphology;
        std::string error;
        QVERIFY(morphology.load(path, &error));


        // Several lemmas of one form.
        const std::vector<Reading> mam = morphology.analyse("mam");
        QVERIFY(mam.size() == 3);
        QVERIFY(has(mam, "mama", "subst:pl:gen:f", "nazwa_pospolita"));
        QVERIFY(has(mam, "mieć", "fin:sg:pri:imperf"));
        QVERIFY(has(mam, "mamić", "impt:sg:sec:imperf"));
        // A form that is the beginning of another, and the other way round.
        QVERIFY(morphology.analyse("mama").size() == 1);
        QVERIFY(morphology.analyse("ma").empty());
        QVERIFY(morphology.analyse("mamy").empty());
        QVERIFY(morphology.contains("mama"));
        QVERIFY(!morphology.contains("ma"));
        // A lemma made by cutting the start ("naj") and the end.
        QVERIFY(has(morphology.analyse("najładniejszego"), "ładny", "adj:sg:gen:m1.m2.m3.n:sup"));
        // A line twice gives one reading.
        QVERIFY(morphology.analyse("piękną").size() == 2);
        // Qualifiers and labels kept.
        const std::vector<Reading> dziewczynie = morphology.analyse("dziewczynie");
        QVERIFY(dziewczynie.size() == 2);
        QVERIFY(has(dziewczynie, "dziewczyni", "adj:sg:nom.voc:n:pos", "", "daw."));
        // A lemma longer than its form, with another case.
        QVERIFY(has(morphology.analyse("Ab"), "Księga_Abdiasza", "brev:npun", "tytuł"));
        QVERIFY(morphology.analyse("ab").empty());
        QVERIFY(has(morphology.analyse("szybko"), "szybki", "adv:pos"));
        QVERIFY(morphology.analyse("").empty());

        // Not a morphology file.
        Morphology other;
        QVERIFY(!other.load(file("text.txt", "mam\tmama\n"), &error));
        QVERIFY(!other.load(file("short.morph", "SPMORPH2"), &error));
        // A lemma too unlike its form is refused.
        MorphologyBuilder builder;
        QVERIFY(!builder.add("", "x", "subst"));
    }

    void grammar() {
        // Words and other characters.
        const std::vector<Token> tokens = tokenize("Ala, ma-kota.");
        QCOMPARE(tokens.size(), size_t(6));
        QVERIFY(tokens[0].word && tokens[0].text == "Ala" && !tokens[1].word && tokens[3].text == "-");
        QCOMPARE(tokens[4].offset, size_t(8));

        std::string error;
        GrammarRules rules;
        QVERIFY(loadGrammarRules(QCE_GRAMMAR_DATA_DIR "/pl.grammar", rules, &error));
        QVERIFY(rules.letters.size() == 32 && rules.keyboard.size() == 3 && !rules.idioms.empty());
        QVERIFY(!loadGrammarRules(file("bad.grammar", "[nonsense]\nx\n"), rules, &error));
        QVERIFY(loadGrammarRules(QCE_GRAMMAR_DATA_DIR "/pl.grammar", rules, &error));

        Morphology morphology;
        QVERIFY(morphology.load(build("grammar.morph", kGrammarDictionary), &error));
        Grammar grammar(morphology);
        grammar.setRules(rules);

    // Slips of the chosen kinds.
        const std::vector<std::string> variants = grammar.slipVariants("ze");
        QVERIFY(std::find(variants.begin(), variants.end(), "że") != variants.end());
        QVERIFY(std::find(variants.begin(), variants.end(), "e") == variants.end());  // the first letter kept
    
    
        // "ze" before a verb: "że" (the archaic reading of "jest" left out).
        const std::vector<Finding> zeJest = check(grammar, "wiem, ze jest");
        QVERIFY(zeJest.size() == 1);
        QVERIFY(suggests(zeJest, "government", 2, "że"));
        QVERIFY(check(grammar, "wiem, że jest").empty());
        // Before a noun phrase in its case; an adverb only before an adjective.
        QVERIFY(check(grammar, "ze mną").empty());
        QVERIFY(check(grammar, "ze zbyt dużym domem").empty());
        QVERIFY(suggests(check(grammar, "ze teraz jest"), "government", 0, "że"));
    
        // An adjective not agreeing with the noun after it.
        QVERIFY(suggests(check(grammar, "piękna dziewczynę"), "agreement", 0, "piękną"));
        QVERIFY(check(grammar, "piękną dziewczynę").empty());
        // A genitive after an adjective: an attribute of the noun before.
        QVERIFY(check(grammar, "sukienkę czerwoną siostry").empty());
        // A dative may be the adjective's complement ("wdzięczna siostrze"), so
        // this one is left alone.
        QVERIFY(check(grammar, "piękna dziewczynie").empty());
    
        // Two words as one, a space typed by mistake, offered first.
        const std::vector<Finding> split = check(grammar, "Po jechałem");
        QVERIFY(!split.empty() && !split.front().suggestions.empty());
        QCOMPARE(split.front().suggestions.front().word, std::string("Pojechałem"));
        QCOMPARE(split.front().suggestions.front().tokens, size_t(2));
        // No word of one letter the language lacks ("ż").
        const std::vector<Finding> pilgrimage = check(grammar, "z pielgrzymka");
        QVERIFY(suggests(pilgrimage, "government", 1, "pielgrzymką"));
        QVERIFY(!suggests(pilgrimage, "government", 0, "ż"));

        // The preposition stays; the noun after it is mended.
        const std::vector<Finding> imitation = check(grammar, "z naśladowanie");
        QVERIFY(suggests(imitation, "government", 1, "naśladowaniem"));
        QVERIFY(!suggests(imitation, "government", 0, "za"));

        // No slip mends it: after the preposition the noun takes its case and
        // the adjective agrees with it, as one change of both.
        const std::vector<Finding> direction = check(grammar, "w przeciwna stroną");
        QVERIFY(!direction.empty() && !direction.front().suggestions.empty());
        QCOMPARE(direction.front().suggestions.front().word, std::string("przeciwną stronę"));
        QCOMPARE(direction.front().suggestions.front().token, size_t(1));
        QCOMPARE(direction.front().suggestions.front().tokens, size_t(2));

        // A word known only as rare, one diacritic from a common one.
        QVERIFY(suggests(check(grammar, "juz jest"), "rare word", 0, "już"));
        QVERIFY(check(grammar, "już jest").empty());
        // Unknown words fit anything.
        QVERIFY(check(grammar, "ze xyzzy").empty());
    
        // Learnt complements: a dative only after the adjectives that take it.
        Grammar learnt(morphology);
        learnt.setRules(rules);
        QVERIFY(learnt.loadComplements(file("complements.tsv", "# test\nwdzięczny\tdat\t3\t5\n"), &error));
        QVERIFY(learnt.check(tokenize("wdzięczny ojcu")).empty());
        QVERIFY(!learnt.check(tokenize("piękna dziewczynie")).empty());
    
        // Two verbs: "mam" is a verb only with the corpus counts ("mama" seldom),
        // and "Mama" can be the subject of "dzwoniła".
        QVERIFY(check(grammar, "Mam dzwoniła").empty());
        Grammar counted(morphology);
        counted.setRules(rules);
        QVERIFY(counted.loadForms(file("forms.tsv", "# test\nmam\tfin\t50\nmam\tsubst\t1\nmama\tsubst\t20\n"), &error));
        QVERIFY(suggests(counted.check(tokenize("Mam dzwoniła")), "verbs", 0, "Mama"));
        QVERIFY(counted.check(tokenize("mam mam")).empty());
    }
};

QTEST_GUILESS_MAIN(TestGrammar)
#include "test_grammar.moc"
