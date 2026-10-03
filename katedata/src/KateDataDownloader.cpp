#include <qce/kate/KateDataDownloader.h>
#include <qce/kate/KatePaths.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QXmlStreamReader>

namespace qce::kate {

static constexpr int kTransferTimeoutMs = 30000;

static const QString kThemeQrcName = QStringLiteral("theme-data.qrc");

static QByteArray readFile(const QString& path, bool* ok = nullptr) {
    QFile f(path);
    const bool opened = f.open(QIODevice::ReadOnly);
    if (ok) *ok = opened;
    return opened ? f.readAll() : QByteArray();
}

static bool writeFileAtomic(const QString& path, const QByteArray& data) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

// -----------------------------------------------------------------------------
// Pure helpers
// -----------------------------------------------------------------------------

bool KateDataDownloader::isSafeFileName(const QString& name, QStringView suffix) {
    if (name.isEmpty() || name.startsWith(QLatin1Char('.'))) return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) return false;
    if (name.contains(QLatin1String(".."))) return false;
    return name.endsWith(suffix) && name.size() > suffix.size();
}

QList<KateDataDownloader::UpdateEntry> KateDataDownloader::parseUpdateXml(const QByteArray& xml) {
    QList<UpdateEntry> result;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        if (r.name() != QLatin1String("Definition")) continue;
        const auto a = r.attributes();
        UpdateEntry e;
        e.name    = a.value(QLatin1String("name")).toString();
        e.url     = a.value(QLatin1String("url")).toString();
        e.version = a.value(QLatin1String("version")).toInt();
        e.file    = QUrl(e.url).fileName();
        if (isSafeFileName(e.file, u".xml")) result.append(e);
    }
    return result;
}

QStringList KateDataDownloader::parseThemeQrc(const QByteArray& xml) {
    QStringList result;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        if (r.name() != QLatin1String("file")) continue;
        const QString f = r.readElementText().trimmed();
        if (isSafeFileName(f, u".theme")) result.append(f);
    }
    return result;
}

QList<KateDataDownloader::UpdateEntry>
KateDataDownloader::syntaxToDownload(const QList<UpdateEntry>& update,
                                     const KateSyntaxIndex& index) {
    QList<UpdateEntry> result;
    for (const UpdateEntry& u : update) {
        const LanguageEntry* e = index.byFile(u.file);
        if (!e || e->version < u.version) result.append(u);
    }
    return result;
}

QStringList KateDataDownloader::themesToDownload(const QStringList& themes,
                                                 const QString& themesDir) {
    QStringList result;
    const QDir dir(themesDir);
    for (const QString& t : themes)
        if (!dir.exists(t)) result.append(t);
    result.removeDuplicates();
    return result;
}

QList<QUrl> KateDataDownloader::defaultThemesBaseUrls(SyntaxVersion v) {
    const QString base =
        QStringLiteral("https://invent.kde.org/frameworks/syntax-highlighting/-/raw/");
    const QString path = QStringLiteral("/data/themes/");
    return {
        QUrl(base + QStringLiteral("v%1.%2.0").arg(v.major).arg(v.minor) + path),
        QUrl(base + QStringLiteral("master") + path),
    };
}

// -----------------------------------------------------------------------------
// KateDataDownloader
// -----------------------------------------------------------------------------

KateDataDownloader::KateDataDownloader(QObject* parent)
    : QObject(parent),
      m_dataDir(qce::kate::dataDir()),
      m_updateUrl(supportedSyntaxVersion().updateUrl()),
      m_themesBaseUrls(defaultThemesBaseUrls()) {}

KateDataDownloader::~KateDataDownloader() {
    // Replies are children of the manager; abort quietly without emitting.
    const auto replies = m_active.keys();
    m_active.clear();
    for (QNetworkReply* r : replies) {
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    if (m_ownsNam) delete m_nam;
}

void KateDataDownloader::setDataDir(const QString& dir) {
    m_dataDir = QDir::cleanPath(dir);
}

void KateDataDownloader::setNetworkAccessManager(QNetworkAccessManager* nam) {
    if (m_ownsNam) delete m_nam;
    m_nam = nam;
    m_ownsNam = false;
}

QNetworkAccessManager* KateDataDownloader::nam() {
    if (!m_nam) {
        m_nam = new QNetworkAccessManager;
        m_ownsNam = true;
    }
    return m_nam;
}

QNetworkReply* KateDataDownloader::get(const QUrl& url) {
    QNetworkRequest req(url);
    req.setTransferTimeout(kTransferTimeoutMs);
    // invent.kde.org answers 403 to Qt's default "Mozilla/5.0" User-Agent.
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("qcodeedit-katedata (Kate syntax %1)")
                      .arg(supportedSyntaxVersion().toString()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return nam()->get(req);
}

bool KateDataDownloader::mustDownload() const {
    const QString syntaxDir = m_dataDir + QStringLiteral("/syntax");
    const QString themesDir = m_dataDir + QStringLiteral("/themes");

    bool ok = false;
    const QByteArray update =
        readFile(m_dataDir + QLatin1Char('/') + supportedSyntaxVersion().updateFileName(), &ok);
    if (!ok) return true;
    const auto entries = parseUpdateXml(update);
    if (entries.isEmpty()) return true;
    const auto index = KateSyntaxIndex::load(m_dataDir);
    if (!syntaxToDownload(entries, index).isEmpty()) return true;

    const QByteArray qrc = readFile(m_dataDir + QLatin1Char('/') + kThemeQrcName, &ok);
    if (!ok) return true;
    const auto themes = parseThemeQrc(qrc);
    if (themes.isEmpty()) return true;
    return !themesToDownload(themes, themesDir).isEmpty();
}

bool KateDataDownloader::start() {
    if (m_busy) return false;
    if (m_dataDir.isEmpty()) return false;

    m_busy       = true;
    m_manifestOk = true;
    m_queue.clear();
    m_total = m_done = m_downloaded = m_failed = 0;

    QDir().mkpath(m_dataDir + QStringLiteral("/syntax"));
    QDir().mkpath(m_dataDir + QStringLiteral("/themes"));
    m_index = KateSyntaxIndex::load(m_dataDir);

    fetchUpdateXml();
    return true;
}

void KateDataDownloader::cancel() {
    if (!m_busy) return;
    m_queue.clear();
    m_manifestOk = false;
    // Detach first so no handler of this run fires after finish() (or into a
    // later run started from a finished() slot).
    const auto replies = m_active.keys();
    m_active.clear();
    for (QNetworkReply* r : replies) {
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    finish();
}

// --- Phase 1: update-X.Y.xml -------------------------------------------------

void KateDataDownloader::fetchUpdateXml() {
    QNetworkReply* reply = get(m_updateUrl);
    m_active.insert(reply, QString());
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onUpdateXml(reply); });
}

void KateDataDownloader::onUpdateXml(QNetworkReply* reply) {
    m_active.remove(reply);
    reply->deleteLater();
    if (!m_busy) return;

    const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                     : QByteArray();
    const auto entries = parseUpdateXml(data);
    if (entries.isEmpty()) {
        qWarning() << "KateDataDownloader: cannot use" << m_updateUrl.toString()
                   << reply->errorString();
        m_manifestOk = false;
    } else {
        writeFileAtomic(m_dataDir + QLatin1Char('/') + supportedSyntaxVersion().updateFileName(),
                        data);
        const QString syntaxDir = m_dataDir + QStringLiteral("/syntax/");
        for (const UpdateEntry& e : syntaxToDownload(entries, m_index))
            m_queue.enqueue({QUrl(e.url), syntaxDir + e.file});
    }
    fetchThemeQrc(0);
}

// --- Phase 2: theme-data.qrc (tag, then master) -----------------------------

void KateDataDownloader::fetchThemeQrc(int baseIndex) {
    if (baseIndex >= m_themesBaseUrls.size()) {
        qWarning() << "KateDataDownloader: no theme-data.qrc available";
        m_manifestOk = false;
        startJobs();
        return;
    }
    QNetworkReply* reply = get(m_themesBaseUrls[baseIndex].resolved(QUrl(kThemeQrcName)));
    m_active.insert(reply, QString());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, baseIndex] { onThemeQrc(reply, baseIndex); });
}

void KateDataDownloader::onThemeQrc(QNetworkReply* reply, int baseIndex) {
    m_active.remove(reply);
    reply->deleteLater();
    if (!m_busy) return;

    const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                     : QByteArray();
    const auto themes = parseThemeQrc(data);
    if (themes.isEmpty()) {
        qInfo() << "KateDataDownloader: no theme list at" << reply->url().toString()
                << reply->errorString();
        fetchThemeQrc(baseIndex + 1);   // e.g. tag not released yet → master
        return;
    }
    writeFileAtomic(m_dataDir + QLatin1Char('/') + kThemeQrcName, data);
    const QString themesDir = m_dataDir + QStringLiteral("/themes");
    const QUrl base = m_themesBaseUrls[baseIndex];
    for (const QString& t : themesToDownload(themes, themesDir))
        m_queue.enqueue({base.resolved(QUrl(t)), themesDir + QLatin1Char('/') + t});
    startJobs();
}

// --- Phase 3: files ----------------------------------------------------------

void KateDataDownloader::startJobs() {
    m_total = int(m_queue.size());
    emit progress(0, m_total);
    if (m_queue.isEmpty()) {
        finish();
        return;
    }
    pumpJobs();
}

void KateDataDownloader::pumpJobs() {
    while (m_busy && m_active.size() < m_maxParallel && !m_queue.isEmpty()) {
        const Job job = m_queue.dequeue();
        QNetworkReply* reply = get(job.url);
        m_active.insert(reply, job.target);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, target = job.target] { onJob(reply, target); });
    }
}

void KateDataDownloader::onJob(QNetworkReply* reply, const QString& target) {
    m_active.remove(reply);
    reply->deleteLater();
    if (!m_busy) return;

    const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->readAll()
                                                                     : QByteArray();
    if (!data.isEmpty() && writeFileAtomic(target, data)) {
        ++m_downloaded;
        if (target.endsWith(QLatin1String(".xml")))
            m_index.invalidate(QFileInfo(target).fileName());
    } else {
        ++m_failed;
        qWarning() << "KateDataDownloader: failed" << reply->url().toString()
                   << reply->errorString();
    }
    ++m_done;
    emit progress(m_done, m_total);

    if (m_queue.isEmpty() && m_active.isEmpty())
        finish();
    else
        pumpJobs();
}

void KateDataDownloader::finish() {
    if (!m_busy) return;
    m_busy = false;
    m_queue.clear();
    m_index.refresh();
    m_index.saveIfDirty();
    emit finished(m_manifestOk && m_failed == 0, m_downloaded, m_failed);
}

} // namespace qce::kate
