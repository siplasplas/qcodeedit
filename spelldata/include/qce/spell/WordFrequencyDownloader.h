#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace qce::spell {

/// Fetches a list of word forms with their counts for a dictionary's
/// language and turns it into the word frequencies of its suggestions (see
/// buildWordFrequencies() in SpellChecker.h): the list is
/// FrequencyWords' <base>/<language>/<language>_50k.txt (the 50,000 most
/// frequent forms in film subtitles, CC-BY-SA 4.0, under 1 MB), the stems are
/// counted with the installed dictionary in a worker thread, and only the
/// result is kept, in ~/.cache/siplasplas/spell/freq. The class shows no UI:
/// the application asks the user before start().
class WordFrequencyDownloader : public QObject {
    Q_OBJECT
public:
    explicit WordFrequencyDownloader(QObject* parent = nullptr);
    ~WordFrequencyDownloader() override;

    /// https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/
    static QUrl defaultBaseUrl();
    /// Directory (ending in '/') with <language>/<language>_50k.txt; defaults
    /// to defaultBaseUrl() (tests use file:// URLs).
    void setBaseUrl(const QUrl& url) { m_baseUrl = url; }
    /// The list for a language code of a dictionary ("pl", "en"; "nb" and
    /// "nn" -> "no").
    static QUrl listUrl(const QUrl& baseUrl, const QString& language);
    /// For the question to the user, e.g. "FrequencyWords (film subtitles,
    /// CC-BY-SA 4.0)".
    static QString sourceDescription();

    /// Use an application-wide QNetworkAccessManager (not owned). By default
    /// the downloader creates its own on first use.
    void setNetworkAccessManager(QNetworkAccessManager* nam);

    /// Begins fetching the list for the installed dictionary (e.g. "pl_PL")
    /// asynchronously; finished() follows. False when busy.
    bool start(const QString& dictionaryName);
    /// Stops the download (finished(false, …) follows); counting stems that
    /// has begun ends on its own.
    void cancel();
    bool busy() const { return m_busy; }

signals:
    /// Once per start(): `ok` when the frequency file was made; `error`
    /// otherwise (no such list, network, dictionary, cancelled).
    void finished(bool ok, const QString& error);

private:
    QNetworkAccessManager* nam();
    void onReply(QNetworkReply* reply);
    void finish(bool ok, const QString& error);

    QUrl m_baseUrl;
    QNetworkAccessManager* m_nam = nullptr;
    bool m_ownsNam = false;
    bool m_busy = false;
    QString m_dictionary;
    QNetworkReply* m_reply = nullptr;
    QFutureWatcher<QString> m_build;
};

} // namespace qce::spell
