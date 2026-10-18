#include <qce/spell/SpellChecker.h>
#include <qce/spell/WordFrequencyDownloader.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace qce::spell;

// Offline: the "server" is a temporary directory reached through file://
// URLs. Uses the system's Polish dictionary (skips without it); the result
// goes to a temporary XDG_CACHE_HOME.
class TestSpellDataDownloader : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_home;
    QTemporaryDir m_server;

    QUrl serverUrl() const { return QUrl::fromLocalFile(m_server.path() + QLatin1Char('/')); }

private slots:
    void initTestCase() {
        QVERIFY(m_home.isValid());
        QVERIFY(m_server.isValid());
        qputenv("XDG_DATA_HOME", m_home.filePath(QStringLiteral("data")).toLocal8Bit());
        qputenv("XDG_CACHE_HOME", m_home.filePath(QStringLiteral("cache")).toLocal8Bit());
    }

    void listUrl() {
        const QUrl base(QStringLiteral("https://example.com/content/2018/"));
        QCOMPARE(WordFrequencyDownloader::listUrl(base, QStringLiteral("pl")),
                 QUrl(QStringLiteral("https://example.com/content/2018/pl/pl_50k.txt")));
        QCOMPARE(WordFrequencyDownloader::listUrl(base, QStringLiteral("nb")),
                 QUrl(QStringLiteral("https://example.com/content/2018/no/no_50k.txt")));
        QVERIFY(WordFrequencyDownloader::defaultBaseUrl().toString().contains(QStringLiteral("FrequencyWords")));
    }

    void downloadsAndCountsStems() {
        if (dictionaryFor(QStringLiteral("pl")).isEmpty()) QSKIP("No Polish dictionary (hunspell-pl)");
        if (hasWordFrequencies(QStringLiteral("pl"))) QSKIP("Polish word frequencies installed system-wide");
        QDir().mkpath(m_server.filePath(QStringLiteral("pl")));
        QFile list(m_server.filePath(QStringLiteral("pl/pl_50k.txt")));
        QVERIFY(list.open(QIODevice::WriteOnly));
        list.write("działać 6000\ndziałają 3000\nrzeczy 1000\n");
        list.close();

        WordFrequencyDownloader downloader;
        downloader.setBaseUrl(serverUrl());
        QSignalSpy finished(&downloader, &WordFrequencyDownloader::finished);
        QVERIFY(downloader.start(QStringLiteral("pl_PL")));
        QVERIFY(downloader.busy());
        QVERIFY(!downloader.start(QStringLiteral("pl_PL")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QVERIFY(!downloader.busy());
        QVERIFY(hasWordFrequencies(QStringLiteral("pl")));

        QFile file(m_home.filePath(QStringLiteral("cache/siplasplas/spell/freq/pl.lemma.freq")));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.startsWith(QStringLiteral("# 10000 words in pl_50k.txt, 10000 known to pl_PL\n")));
        QVERIFY(text.contains(QStringLiteral("\ndziałać\t9000\n")));
    }

    void missingListFails() {
        WordFrequencyDownloader downloader;
        downloader.setBaseUrl(serverUrl());
        QSignalSpy finished(&downloader, &WordFrequencyDownloader::finished);
        const QString dictionary = dictionaryFor(QStringLiteral("en"));
        // Without an English list on the "server", or without the dictionary.
        QVERIFY(downloader.start(dictionary.isEmpty() ? QStringLiteral("xx_XX") : dictionary));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY(!finished.at(0).at(0).toBool());
        QVERIFY(!finished.at(0).at(1).toString().isEmpty());
        QVERIFY(!downloader.busy());
    }
};

QTEST_GUILESS_MAIN(TestSpellDataDownloader)
#include "test_spell_data_downloader.moc"
