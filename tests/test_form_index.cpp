#include "FormIndex.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

using namespace qce::spell::detail;

// expandDictionary() on small dictionaries: every kind of flags, flag aliases,
// NEEDAFFIX, and what is not supported.
class TestFormIndex : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_count = 0;

    // The forms of a dictionary from these .aff and .dic texts (UTF-8),
    // sorted, without repeats; `supported` tells whether it was unfolded.
    QStringList expand(const QByteArray& aff, const QByteArray& dic, bool* supported = nullptr) {
        const QString base = m_dir.filePath(QString::number(++m_count));
        for (const auto& [suffix, text] : {std::pair{".aff", aff}, std::pair{".dic", dic}}) {
            QFile file(base + QLatin1String(suffix));
            if (!file.open(QIODevice::WriteOnly)) return {};
            file.write(text);
        }
        bool ok = false;
        QStringList forms = expandDictionary(base + QStringLiteral(".aff"), base + QStringLiteral(".dic"), &ok);
        if (supported) *supported = ok;
        std::sort(forms.begin(), forms.end());
        forms.erase(std::unique(forms.begin(), forms.end()), forms.end());
        return forms;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void characterFlags() {
        bool supported = false;
        const QStringList forms = expand("SET UTF-8\n"
                                         "SFX S Y 1\n"
                                         "SFX S 0 s .\n"
                                         "PFX U Y 1\n"
                                         "PFX U 0 un .\n",
                                         "2\nkind/SU\ncat/S\n", &supported);
        QVERIFY(supported);
        // A prefix and a suffix both crossing: together too.
        QCOMPARE(forms, (QStringList{"cat", "cats", "kind", "kinds", "unkind", "unkinds"}));
    }

    void longFlags() {
        const QStringList forms = expand("SET UTF-8\nFLAG long\n"
                                         "SFX Aa Y 1\n"
                                         "SFX Aa 0 er .\n"
                                         "SFX Ab Y 1\n"
                                         "SFX Ab 0 est .\n",
                                         "1\nfast/AaAb\n");
        QCOMPARE(forms, (QStringList{"fast", "faster", "fastest"}));
    }

    void numericFlags() {
        const QStringList forms = expand("SET UTF-8\nFLAG num\n"
                                         "SFX 101 Y 1\n"
                                         "SFX 101 0 ing .\n"
                                         "SFX 7 Y 1\n"
                                         "SFX 7 0 ed .\n"
                                         "SFX 10 Y 1\n"
                                         "SFX 10 0 s .\n",
                                         "1\nwalk/101,7\n");
        // 10 is not among the flags (nor 1 or 0 of 101).
        QCOMPARE(forms, (QStringList{"walk", "walked", "walking"}));
    }

    void utf8Flags() {
        // A flag outside the BMP is one flag, not two UTF-16 units.
        const QStringList forms = expand("SET UTF-8\nFLAG UTF-8\n"
                                         "SFX ą Y 1\n"
                                         "SFX ą 0 y .\n"
                                         "SFX 𝔸 Y 1\n"
                                         "SFX 𝔸 0 ami .\n",
                                         "1\nkot/ą𝔸\n");
        QCOMPARE(forms, (QStringList{"kot", "kotami", "koty"}));
    }

    void flagAliases() {
        const QStringList forms = expand("SET UTF-8\n"
                                         "AF 2\n"
                                         "AF A\n"
                                         "AF AB\n"
                                         "SFX A Y 1\n"
                                         "SFX A 0 s .\n"
                                         "SFX B Y 1\n"
                                         "SFX B 0 ful .\n",
                                         "2\nhope/2\ncup/1\n");
        QCOMPARE(forms, (QStringList{"cup", "cups", "hope", "hopeful", "hopes"}));
    }

    void stemNeedingAnAffix() {
        // "beautif" is no word, only its forms are.
        const QStringList forms = expand("SET UTF-8\nNEEDAFFIX X\n"
                                         "SFX A Y 2\n"
                                         "SFX A 0 y .\n"
                                         "SFX A 0 ul .\n",
                                         "2\nbeautif/AX\nbeauty\n");
        QCOMPARE(forms, (QStringList{"beautiful", "beautify", "beauty"}));
    }

    void forbiddenWordsLeftOut() {
        const QStringList forms = expand("SET UTF-8\nFORBIDDENWORD !\n", "2\ngood\ngoood/!\n");
        QCOMPARE(forms, QStringList{"good"});
    }

    void ruleConditions() {
        // Strip "y" after a consonant only; "s" after other endings.
        const QStringList forms = expand("SET UTF-8\n"
                                         "SFX S Y 2\n"
                                         "SFX S y ies [^aeiou]y\n"
                                         "SFX S 0 s [aeiou]y\n",
                                         "2\ncity/S\nday/S\n");
        QCOMPARE(forms, (QStringList{"cities", "city", "day", "days"}));
    }

    void twoSuffixes() {
        // "able" allows "s" after it (hunspell's example): drinkables.
        const QStringList forms = expand("SET UTF-8\n"
                                         "SFX X Y 1\n"
                                         "SFX X 0 able/Y .\n"
                                         "SFX Y Y 1\n"
                                         "SFX Y 0 s .\n",
                                         "1\ndrink/X\n");
        QCOMPARE(forms, (QStringList{"drink", "drinkable", "drinkables"}));
    }

    void prefixesAllowedBySuffixes() {
        // French elision: a conjugated form takes "l'" and "n'", the bare
        // stem does not (its flags have no prefix).
        const QStringList forms = expand("SET UTF-8\nFLAG long\n"
                                         "PFX L' Y 1\n"
                                         "PFX L' 0 l' .\n"
                                         "PFX N' Y 1\n"
                                         "PFX N' 0 n' .\n"
                                         "SFX a1 Y 1\n"
                                         "SFX a1 er e/L'N' er\n",
                                         "1\naimer/a1\n");
        QCOMPARE(forms, (QStringList{"aime", "aimer", "l'aime", "n'aime"}));
    }

    void suffixesAllowedByPrefixes() {
        // "re" allows "s" after the prefixed word only.
        const QStringList forms = expand("SET UTF-8\n"
                                         "PFX R Y 1\n"
                                         "PFX R 0 re/S .\n"
                                         "SFX S Y 1\n"
                                         "SFX S 0 s .\n",
                                         "1\nmake/R\n");
        QCOMPARE(forms, (QStringList{"make", "remake", "remakes"}));
    }

    void circumfixOnlyTogether() {
        // German participles: "ge" and "t" make a word together, not alone.
        const QStringList forms = expand("SET UTF-8\nCIRCUMFIX C\n"
                                         "PFX G Y 1\n"
                                         "PFX G 0 ge/C .\n"
                                         "SFX T Y 2\n"
                                         "SFX T en t/C en\n"
                                         "SFX T en e en\n",
                                         "1\nmachen/GT\n");
        QCOMPARE(forms, (QStringList{"gemacht", "mache", "machen"}));
    }

    void suffixNeedingAnotherAffix() {
        // "ation" is no word end alone here, only with "s" after it.
        const QStringList forms = expand("SET UTF-8\nNEEDAFFIX N\n"
                                         "SFX A Y 1\n"
                                         "SFX A 0 ation/NB .\n"
                                         "SFX B Y 1\n"
                                         "SFX B 0 s .\n",
                                         "1\nform/A\n");
        QCOMPARE(forms, (QStringList{"form", "formations"}));
    }

    void forbiddenFormsAndCompoundParts() {
        // A forbidden word in the .dic removes the form a rule makes (Dutch
        // "Kongs"); a form ending or beginning with a hyphen is a part of
        // compound words only.
        const QStringList forms = expand("SET UTF-8\nFORBIDDENWORD !\n"
                                         "SFX S Y 1\n"
                                         "SFX S 0 s .\n"
                                         "SFX H Y 1\n"
                                         "SFX H 0 - .\n",
                                         "3\nKong/S\nKongs/!\napk/H\n");
        QCOMPARE(forms, (QStringList{"Kong", "apk"}));
    }

    void byteOrderMark() {
        // pt_BR's files begin with one: SET is still read.
        const QStringList forms = expand("\xEF\xBB\xBFSET UTF-8\nFLAG UTF-8\n"
                                         "SFX ã Y 1\n"
                                         "SFX ã 0 ões .\n",
                                         "\xEF\xBB\xBF" "1\nCamar/ã\n");
        QCOMPARE(forms, (QStringList{"Camar", "Camarões"}));
    }

    void accentsKeepTheClass() {
        // French typed without accents: the same class, a cheap difference.
        QCOMPARE(classKey(QStringLiteral("societe")), classKey(QStringLiteral("société")));
        QCOMPARE(classKey(QStringLiteral("Francais")), classKey(QStringLiteral("Français")));
        QCOMPARE(classKey(QStringLiteral("zolw")), classKey(QStringLiteral("żółw")));
        QVERIFY(score(QStringLiteral("societe"), QStringLiteral("société"), kSuggestionLimit) < 1.0);
    }

    void unsupportedLeftToHunspell() {
        bool supported = true;
        QVERIFY(expand("SET UTF-8\nCOMPLEXPREFIXES\n", "1\nword\n", &supported).isEmpty());
        QVERIFY(!supported);
    }
};

QTEST_GUILESS_MAIN(TestFormIndex)
#include "test_form_index.moc"
