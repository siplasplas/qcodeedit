#include <qce/spell/WordFrequencyDownloader.h>

#include <qce/spell/SpellChecker.h>

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>

namespace qce::spell {

namespace {

// A 50k list is under 1 MB; anything much larger is not one.
constexpr qint64 kMaxListSize = 16 * 1024 * 1024;

} // namespace

WordFrequencyDownloader::WordFrequencyDownloader(QObject* parent)
    : QObject(parent), m_baseUrl(defaultBaseUrl()) {
    connect(&m_build, &QFutureWatcher<QString>::finished, this, [this]() {
        const QString error = m_build.result();
        finish(error.isEmpty(), error);
    });
}

WordFrequencyDownloader::~WordFrequencyDownloader() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    m_build.waitForFinished();
    if (m_ownsNam) delete m_nam;
}

QUrl WordFrequencyDownloader::defaultBaseUrl() {
    return QUrl(QStringLiteral("https://raw.githubusercontent.com/hermitdave/FrequencyWords/master/content/2018/"));
}

QUrl WordFrequencyDownloader::listUrl(const QUrl& baseUrl, const QString& language) {
    QString code = language.toLower();
    if (code == QLatin1String("nb") || code == QLatin1String("nn")) code = QStringLiteral("no");
    return baseUrl.resolved(QUrl(QStringLiteral("%1/%1_50k.txt").arg(code)));
}

QString WordFrequencyDownloader::sourceDescription() {
    return QStringLiteral("FrequencyWords (film subtitles, CC-BY-SA 4.0)");
}

void WordFrequencyDownloader::setNetworkAccessManager(QNetworkAccessManager* nam) {
    if (m_ownsNam) delete m_nam;
    m_nam = nam;
    m_ownsNam = false;
}

QNetworkAccessManager* WordFrequencyDownloader::nam() {
    if (!m_nam) {
        m_nam = new QNetworkAccessManager;
        m_ownsNam = true;
    }
    return m_nam;
}

bool WordFrequencyDownloader::start(const QString& dictionaryName) {
    if (m_busy) return false;
    m_busy = true;
    m_dictionary = dictionaryName;
    QString language;
    for (const Dictionary& dictionary : installedDictionaries())
        if (dictionary.name == dictionaryName) language = dictionary.language;
    if (language.isEmpty()) {
        // Reported after start() returns, as for any other failure.
        QMetaObject::invokeMethod(this, [this, dictionaryName]() {
            finish(false, tr("The dictionary %1 is not installed").arg(dictionaryName));
        }, Qt::QueuedConnection);
        return true;
    }
    QNetworkRequest request(listUrl(m_baseUrl, language));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = nam()->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        if (received > kMaxListSize && m_reply) m_reply->abort();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply]() { onReply(reply); });
    return true;
}

void WordFrequencyDownloader::cancel() {
    if (m_reply) m_reply->abort();
}

void WordFrequencyDownloader::onReply(QNetworkReply* reply) {
    reply->deleteLater();
    m_reply = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        finish(false, reply->error() == QNetworkReply::ContentNotFoundError
                          ? tr("No word frequency list for this language (%1)").arg(reply->url().toString())
                          : reply->errorString());
        return;
    }
    const QByteArray list = reply->readAll();
    if (list.isEmpty() || list.size() > kMaxListSize) {
        finish(false, tr("Unexpected word frequency list from %1").arg(reply->url().toString()));
        return;
    }
    // The list is kept as it is in the shared directory, the stems are
    // counted from it in a worker thread.
    m_build.setFuture(QtConcurrent::run([list, dictionary = m_dictionary]() -> QString {
        const QDir dir(wordFrequencyListDir());
        if (!dir.mkpath(QStringLiteral(".")))
            return QStringLiteral("Cannot create %1").arg(dir.path());
        if (!dir.exists(QStringLiteral("README.txt"))) {
            QSaveFile readme(dir.filePath(QStringLiteral("README.txt")));
            if (readme.open(QIODevice::WriteOnly | QIODevice::Text)) {
                readme.write("Lists of the most frequent word forms of a language with their counts,\n"
                             "<language>_50k.txt, downloaded by programs using qcodeedit (spelling\n"
                             "suggestions) and shared by them.\n\n"
                             "Source: FrequencyWords by Hermit Dave, from OpenSubtitles 2018,\n"
                             "https://github.com/hermitdave/FrequencyWords\n"
                             "Licence: CC-BY-SA 4.0, https://creativecommons.org/licenses/by-sa/4.0/\n");
                readme.commit();
            }
        }
        const QString path = wordFrequencyListPath(dictionary);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(list) != list.size() || !file.commit())
            return QStringLiteral("Cannot write %1").arg(path);
        QString error;
        return buildWordFrequencies(dictionary, path, &error) ? QString() : error;
    }));
}

void WordFrequencyDownloader::finish(bool ok, const QString& error) {
    m_busy = false;
    emit finished(ok, error);
}

} // namespace qce::spell
