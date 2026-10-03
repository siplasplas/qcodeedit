#pragma once

#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateSyntaxVersion.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace qce::kate {

/// Fetches Kate syntax definitions and themes into a qcodeedit data directory
/// (see KatePaths.h), downloading only what is missing or outdated.
///
///  - Syntax: update-<major>.<minor>.xml from kate-editor.org lists every
///    definition with its version. A file is fetched if it is not in
///    <dataDir>/syntax or the KateSyntaxIndex holds a lower version.
///  - Themes: theme-data.qrc lists the *.theme files. It is fetched from the
///    release tag v<major>.<minor>.0 when that exists, otherwise from master.
///    Only missing themes are fetched.
///
/// After the downloads index.json is refreshed and saved. The class shows no
/// UI; the application decides when to call mustDownload()/start() and how to
/// present progress.
class KateDataDownloader : public QObject {
    Q_OBJECT
public:
    /// One <Definition> from update-X.Y.xml.
    struct UpdateEntry {
        QString name;
        QString url;
        QString file;     ///< last URL segment, e.g. "cpp.xml"
        int     version = 0;
    };

    explicit KateDataDownloader(QObject* parent = nullptr);
    ~KateDataDownloader() override;

    /// Target directory; defaults to qce::kate::dataDir().
    void setDataDir(const QString& dir);
    QString dataDir() const { return m_dataDir; }

    /// Source of update-X.Y.xml; defaults to supportedSyntaxVersion().updateUrl().
    void setUpdateUrl(const QUrl& url) { m_updateUrl = url; }

    /// Directories (ending in '/') tried in order for theme-data.qrc and the
    /// *.theme files; defaults to defaultThemesBaseUrls().
    void setThemesBaseUrls(const QList<QUrl>& urls) { m_themesBaseUrls = urls; }

    /// Use an application-wide QNetworkAccessManager (not owned). By default
    /// the downloader creates its own on first use.
    void setNetworkAccessManager(QNetworkAccessManager* nam);

    /// Number of parallel file downloads (default 6).
    void setMaxParallel(int n) { m_maxParallel = qMax(1, n); }

    /// Cheap, offline check based on the files saved by the previous run
    /// (update-X.Y.xml, theme-data.qrc) and the current directory contents.
    /// True if nothing was downloaded yet or something is missing/outdated.
    bool mustDownload() const;

    /// Begin downloading asynchronously. Returns false if already busy.
    bool start();

    /// Abort all requests; finished(false, …) is emitted.
    void cancel();

    bool busy() const { return m_busy; }

    // --- Pure helpers (public for tests and tools) --------------------------

    static QList<UpdateEntry> parseUpdateXml(const QByteArray& xml);
    static QStringList        parseThemeQrc(const QByteArray& xml);

    /// Entries whose file is absent from `index` or has a lower version there.
    static QList<UpdateEntry> syntaxToDownload(const QList<UpdateEntry>& update,
                                               const KateSyntaxIndex& index);

    /// Theme file names not present in `themesDir`.
    static QStringList themesToDownload(const QStringList& themes,
                                        const QString& themesDir);

    /// {…/raw/v<major>.<minor>.0/data/themes/, …/raw/master/data/themes/}
    static QList<QUrl> defaultThemesBaseUrls(SyntaxVersion v = supportedSyntaxVersion());

    /// True for a plain file name with the given suffix: no path separators,
    /// no "..", not hidden. Guards against writing outside the target dirs.
    static bool isSafeFileName(const QString& name, QStringView suffix);

signals:
    /// File downloads completed so far out of the total queued.
    void progress(int done, int total);

    /// Emitted once per start(). `ok` is false if a manifest could not be
    /// fetched, any file failed, or the run was cancelled.
    void finished(bool ok, int downloaded, int failed);

private:
    struct Job {
        QUrl    url;
        QString target;
    };

    QNetworkAccessManager* nam();
    QNetworkReply* get(const QUrl& url);

    void fetchUpdateXml();
    void onUpdateXml(QNetworkReply* reply);
    void fetchThemeQrc(int baseIndex);
    void onThemeQrc(QNetworkReply* reply, int baseIndex);
    void startJobs();
    void pumpJobs();
    void onJob(QNetworkReply* reply, const QString& target);
    void finish();

    QString       m_dataDir;
    QUrl          m_updateUrl;
    QList<QUrl>   m_themesBaseUrls;
    int           m_maxParallel = 6;

    QNetworkAccessManager* m_nam      = nullptr;
    bool                   m_ownsNam  = false;

    // Per-run state.
    bool                   m_busy       = false;
    bool                   m_manifestOk = true;
    KateSyntaxIndex        m_index;
    QQueue<Job>            m_queue;
    QHash<QNetworkReply*, QString> m_active;  // reply → target ("" for manifests)
    int                    m_total      = 0;
    int                    m_done       = 0;
    int                    m_downloaded = 0;
    int                    m_failed     = 0;
};

} // namespace qce::kate
