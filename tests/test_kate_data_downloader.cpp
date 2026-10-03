#include <qce/kate/KateDataDownloader.h>
#include <qce/kate/KateSyntaxIndex.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using qce::kate::KateDataDownloader;
using qce::kate::KateSyntaxIndex;

// Every test runs offline: the "server" is a temp directory reached through
// file:// URLs, which QNetworkAccessManager handles like any other scheme.

namespace {

QByteArray langXml(const QString& name, int version) {
    return QStringLiteral(
               "<?xml version=\"1.0\"?>\n"
               "<language name=\"%1\" version=\"%2\" kateversion=\"5.0\" "
               "section=\"Other\" extensions=\"*.%1\"><highlighting/></language>\n")
        .arg(name)
        .arg(version)
        .toUtf8();
}

void writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestKateDataDownloader : public QObject {
    Q_OBJECT

private slots:
    void init();

    void isSafeFileName();
    void parseUpdateXml();
    void parseThemeQrc();
    void syntaxToDownload();
    void themesToDownload();
    void defaultThemesBaseUrls();

    void fullRun_downloadsOnlyWhatIsNeeded();
    void fullRun_secondRunDownloadsNothing();
    void missingFile_reportsFailureAndWritesNothing();
    void missingUpdateXml_stillFetchesThemes();
    void cancel_emitsFinishedOnce();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString remote() const { return m_tmp->path() + QStringLiteral("/remote"); }
    QString data() const   { return m_tmp->path() + QStringLiteral("/data"); }
    QUrl remoteUrl(const QString& rel) const { return QUrl::fromLocalFile(remote() + QLatin1Char('/') + rel); }

    /// remote/update.xml lists a.xml (v2) and b.xml (v1);
    /// remote/tag/ has no qrc (unreleased tag); remote/master/ has x and y themes.
    void populateRemote(const QStringList& extraDefinitions = {});
    void configure(KateDataDownloader& dl) const;
    static bool runToEnd(KateDataDownloader& dl, QList<QVariant>* finishedArgs);
};

void TestKateDataDownloader::init() {
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
}

void TestKateDataDownloader::populateRemote(const QStringList& extraDefinitions) {
    writeFile(remote() + QStringLiteral("/syntax/a.xml"), langXml(QStringLiteral("a"), 2));
    writeFile(remote() + QStringLiteral("/syntax/b.xml"), langXml(QStringLiteral("b"), 1));

    QString update = QStringLiteral("<!DOCTYPE DEFINITIONS>\n<DEFINITIONS>\n");
    update += QStringLiteral("<Definition name=\"a\" url=\"%1\" version=\"2\"/>\n")
                  .arg(remoteUrl(QStringLiteral("syntax/a.xml")).toString());
    update += QStringLiteral("<Definition name=\"b\" url=\"%1\" version=\"1\"/>\n")
                  .arg(remoteUrl(QStringLiteral("syntax/b.xml")).toString());
    for (const QString& d : extraDefinitions) update += d + QLatin1Char('\n');
    update += QStringLiteral("</DEFINITIONS>\n");
    writeFile(remote() + QStringLiteral("/update.xml"), update.toUtf8());

    QDir().mkpath(remote() + QStringLiteral("/tag"));
    writeFile(remote() + QStringLiteral("/master/theme-data.qrc"),
              "<!DOCTYPE RCC><RCC version=\"1.0\"><qresource prefix=\"/t\">"
              "<file>x.theme</file><file>y.theme</file></qresource></RCC>");
    writeFile(remote() + QStringLiteral("/master/x.theme"), R"({"metadata":{"name":"X"}})");
    writeFile(remote() + QStringLiteral("/master/y.theme"), R"({"metadata":{"name":"Y"}})");
}

void TestKateDataDownloader::configure(KateDataDownloader& dl) const {
    dl.setDataDir(data());
    dl.setUpdateUrl(remoteUrl(QStringLiteral("update.xml")));
    dl.setThemesBaseUrls({remoteUrl(QStringLiteral("tag/")), remoteUrl(QStringLiteral("master/"))});
}

bool TestKateDataDownloader::runToEnd(KateDataDownloader& dl, QList<QVariant>* finishedArgs) {
    QSignalSpy spy(&dl, &KateDataDownloader::finished);
    if (!dl.start()) return false;
    if (spy.isEmpty() && !spy.wait(10000)) return false;
    if (spy.size() != 1) return false;
    *finishedArgs = spy.takeFirst();
    return true;
}

// --- Pure helpers ------------------------------------------------------------

void TestKateDataDownloader::isSafeFileName() {
    QVERIFY( KateDataDownloader::isSafeFileName(QStringLiteral("cpp.xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral("cpp.theme"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral(".xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral(".hidden.xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral("../evil.xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral("a/b.xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QStringLiteral("a\\b.xml"), u".xml"));
    QVERIFY(!KateDataDownloader::isSafeFileName(QString(), u".xml"));
}

void TestKateDataDownloader::parseUpdateXml() {
    const QByteArray xml =
        "<!DOCTYPE DEFINITIONS><DEFINITIONS>"
        "<Definition name=\"C++\" url=\"https://kate-editor.org/syntax/data/syntax/cpp.xml\" version=\"33\"/>"
        "<Definition name=\"Bad\" url=\"https://example.org/x/evil.sh\" version=\"1\"/>"
        "<Definition name=\"Hidden\" url=\"https://example.org/x/.h.xml\" version=\"1\"/>"
        "</DEFINITIONS>";
    const auto list = KateDataDownloader::parseUpdateXml(xml);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list[0].name, QStringLiteral("C++"));
    QCOMPARE(list[0].file, QStringLiteral("cpp.xml"));
    QCOMPARE(list[0].version, 33);
    QVERIFY(KateDataDownloader::parseUpdateXml("not xml").isEmpty());
}

void TestKateDataDownloader::parseThemeQrc() {
    const QByteArray qrc =
        "<!DOCTYPE RCC><RCC version=\"1.0\"><qresource prefix=\"/p\">"
        "<file>a.theme</file><file> b.theme </file><file>../c.theme</file>"
        "<file>sub/d.theme</file><file>e.txt</file></qresource></RCC>";
    QCOMPARE(KateDataDownloader::parseThemeQrc(qrc),
             (QStringList{QStringLiteral("a.theme"), QStringLiteral("b.theme")}));
}

void TestKateDataDownloader::syntaxToDownload() {
    writeFile(data() + QStringLiteral("/syntax/a.xml"), langXml(QStringLiteral("a"), 2));
    writeFile(data() + QStringLiteral("/syntax/b.xml"), langXml(QStringLiteral("b"), 5));
    const auto index = KateSyntaxIndex::load(data());

    QList<KateDataDownloader::UpdateEntry> update{
        {QStringLiteral("a"), {}, QStringLiteral("a.xml"), 3},   // newer remotely
        {QStringLiteral("b"), {}, QStringLiteral("b.xml"), 5},   // same
        {QStringLiteral("c"), {}, QStringLiteral("c.xml"), 1},   // missing locally
    };
    const auto todo = KateDataDownloader::syntaxToDownload(update, index);
    QCOMPARE(todo.size(), 2);
    QCOMPARE(todo[0].file, QStringLiteral("a.xml"));
    QCOMPARE(todo[1].file, QStringLiteral("c.xml"));
}

void TestKateDataDownloader::themesToDownload() {
    writeFile(data() + QStringLiteral("/themes/x.theme"), "{}");
    QCOMPARE(KateDataDownloader::themesToDownload(
                 {QStringLiteral("x.theme"), QStringLiteral("y.theme"), QStringLiteral("y.theme")},
                 data() + QStringLiteral("/themes")),
             QStringList{QStringLiteral("y.theme")});
}

void TestKateDataDownloader::defaultThemesBaseUrls() {
    const auto urls = KateDataDownloader::defaultThemesBaseUrls({6, 31});
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls[0].toString(), QStringLiteral(
        "https://invent.kde.org/frameworks/syntax-highlighting/-/raw/v6.31.0/data/themes/"));
    QCOMPARE(urls[1].toString(), QStringLiteral(
        "https://invent.kde.org/frameworks/syntax-highlighting/-/raw/master/data/themes/"));
}

// --- End-to-end over file:// -------------------------------------------------

void TestKateDataDownloader::fullRun_downloadsOnlyWhatIsNeeded() {
    populateRemote();
    // Local state: outdated a.xml (v1) and theme x already present.
    writeFile(data() + QStringLiteral("/syntax/a.xml"), langXml(QStringLiteral("a"), 1));
    writeFile(data() + QStringLiteral("/themes/x.theme"), "local");

    KateDataDownloader dl;
    configure(dl);
    QVERIFY(dl.mustDownload());

    QSignalSpy progress(&dl, &KateDataDownloader::progress);
    QList<QVariant> args;
    QVERIFY(runToEnd(dl, &args));
    QCOMPARE(args.at(0).toBool(), true);
    QCOMPARE(args.at(1).toInt(), 3);   // a.xml (newer), b.xml (missing), y.theme
    QCOMPARE(args.at(2).toInt(), 0);
    QVERIFY(!dl.busy());
    QCOMPARE(progress.last().at(0).toInt(), 3);
    QCOMPARE(progress.last().at(1).toInt(), 3);

    QCOMPARE(readFile(data() + QStringLiteral("/themes/x.theme")), QByteArray("local"));
    QVERIFY(QFile::exists(data() + QStringLiteral("/themes/y.theme")));
    QVERIFY(QFile::exists(data() + QStringLiteral("/update-6.31.xml")));
    QVERIFY(QFile::exists(data() + QStringLiteral("/theme-data.qrc")));

    const auto index = KateSyntaxIndex::load(data());
    QVERIFY(!index.isDirty());   // index.json saved by the downloader
    QCOMPARE(index.byName(QStringLiteral("a"))->version, 2);
    QVERIFY(index.byName(QStringLiteral("b")));

    QVERIFY(!dl.mustDownload());
}

void TestKateDataDownloader::fullRun_secondRunDownloadsNothing() {
    populateRemote();
    KateDataDownloader dl;
    configure(dl);
    QList<QVariant> args;
    QVERIFY(runToEnd(dl, &args));
    QCOMPARE(args.at(1).toInt(), 4);   // a, b, x, y

    QVERIFY(runToEnd(dl, &args));
    QCOMPARE(args.at(0).toBool(), true);
    QCOMPARE(args.at(1).toInt(), 0);
}

void TestKateDataDownloader::missingFile_reportsFailureAndWritesNothing() {
    populateRemote({QStringLiteral("<Definition name=\"gone\" url=\"%1\" version=\"1\"/>")
                        .arg(remoteUrl(QStringLiteral("syntax/gone.xml")).toString())});
    KateDataDownloader dl;
    configure(dl);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("failed.*gone\\.xml")));
    QList<QVariant> args;
    QVERIFY(runToEnd(dl, &args));
    QCOMPARE(args.at(0).toBool(), false);
    QCOMPARE(args.at(1).toInt(), 4);
    QCOMPARE(args.at(2).toInt(), 1);
    QVERIFY(!QFile::exists(data() + QStringLiteral("/syntax/gone.xml")));
    QVERIFY(dl.mustDownload());   // still incomplete
}

void TestKateDataDownloader::missingUpdateXml_stillFetchesThemes() {
    populateRemote();
    QVERIFY(QFile::remove(remote() + QStringLiteral("/update.xml")));
    KateDataDownloader dl;
    configure(dl);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("cannot use")));
    QList<QVariant> args;
    QVERIFY(runToEnd(dl, &args));
    QCOMPARE(args.at(0).toBool(), false);
    QCOMPARE(args.at(1).toInt(), 2);   // x, y
    QVERIFY(QFile::exists(data() + QStringLiteral("/themes/y.theme")));
}

void TestKateDataDownloader::cancel_emitsFinishedOnce() {
    populateRemote();
    KateDataDownloader dl;
    configure(dl);
    QSignalSpy spy(&dl, &KateDataDownloader::finished);
    QVERIFY(dl.start());
    QVERIFY(dl.busy());
    QVERIFY(!dl.start());   // already busy
    dl.cancel();
    QVERIFY(!dl.busy());
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toBool(), false);
    QTest::qWait(200);      // no late handlers
    QCOMPARE(spy.size(), 1);
}

QTEST_GUILESS_MAIN(TestKateDataDownloader)
#include "test_kate_data_downloader.moc"
