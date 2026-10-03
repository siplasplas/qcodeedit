#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateSyntaxVersion.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using qce::kate::KateSyntaxIndex;
using qce::kate::LanguageEntry;
using qce::kate::wildcardMatch;

namespace {

QByteArray langXml(const QString& name, int version, const QString& extensions,
                   const QString& extra = {}) {
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE language>\n"
               "<language name=\"%1\" version=\"%2\" kateversion=\"5.0\" "
               "section=\"Sources\" extensions=\"%3\" %4>\n"
               "  <highlighting><contexts><context name=\"n\" attribute=\"a\"/>"
               "</contexts><itemDatas><itemData name=\"a\" defStyleNum=\"dsNormal\"/>"
               "</itemDatas></highlighting>\n"
               "</language>\n")
        .arg(name)
        .arg(version)
        .arg(extensions, extra)
        .toUtf8();
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

void setMTime(const QString& path, const QDateTime& t) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(t, QFileDevice::FileModificationTime));
}

} // namespace

class TestKateSyntaxIndex : public QObject {
    Q_OBJECT

private slots:
    void init();

    void wildcard();
    void readHeader_fields();
    void readHeader_doctypeWithEntities();
    void readHeader_rejectsNonLanguage();
    void readHeader_unsupportedKateversion();

    void load_emptyDir_isDirtyAndEmpty();
    void saveAndLoad_roundTrip();
    void refresh_unchangedFileIsNotReread();
    void refresh_changedFileIsReread();
    void refresh_removedAndAddedFiles();
    void load_staleSyntaxVersion_rebuilds();
    void load_corruptJson_rebuilds();

    void byName_highestVersionWins();
    void forFileName_priorityAndUnsupported();

    void realData_matchesFileCount();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString dataDir() const { return m_tmp->path(); }
    QString syntaxDir() const { return m_tmp->path() + QStringLiteral("/syntax"); }
    QString xmlPath(const QString& f) const { return syntaxDir() + QLatin1Char('/') + f; }
};

void TestKateSyntaxIndex::init() {
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    QVERIFY(QDir().mkpath(syntaxDir()));
}

void TestKateSyntaxIndex::wildcard() {
    QVERIFY( wildcardMatch(u"main.cpp",       u"*.cpp"));
    QVERIFY(!wildcardMatch(u"main.cpp.bak",   u"*.cpp"));
    QVERIFY( wildcardMatch(u"Makefile",       u"Makefile"));
    QVERIFY( wildcardMatch(u"Makefile.am",    u"Makefile*"));
    QVERIFY(!wildcardMatch(u"makefile",       u"Makefile"));   // case-sensitive
    QVERIFY( wildcardMatch(u"a.h",            u"?.h"));
    QVERIFY(!wildcardMatch(u"ab.h",           u"?.h"));
    QVERIFY( wildcardMatch(u"x",              u"*"));
    QVERIFY( wildcardMatch(u"",               u"*"));
    QVERIFY( wildcardMatch(u"CMakeLists.txt", u"CMakeLists.txt"));
    QVERIFY( wildcardMatch(u"a.tar.gz",       u"*.*.gz"));
    QVERIFY(!wildcardMatch(u"a.gz",           u"*.*.gz"));
    QVERIFY( wildcardMatch(u"abcabd",         u"*abd"));       // needs backtracking
}

void TestKateSyntaxIndex::readHeader_fields() {
    writeFile(xmlPath(QStringLiteral("foo.xml")),
              langXml(QStringLiteral("Foo"), 7, QStringLiteral("*.foo;*.fo"),
                      QStringLiteral("priority=\"5\" hidden=\"true\" mimetype=\"text/x-foo\"")));
    const auto e = KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("foo.xml")));
    QVERIFY(e.has_value());
    QCOMPARE(e->file,        QStringLiteral("foo.xml"));
    QCOMPARE(e->name,        QStringLiteral("Foo"));
    QCOMPARE(e->section,     QStringLiteral("Sources"));
    QCOMPARE(e->version,     7);
    QCOMPARE(e->kateversion, QStringLiteral("5.0"));
    QCOMPARE(e->extensions,  QStringLiteral("*.foo;*.fo"));
    QCOMPARE(e->mimetype,    QStringLiteral("text/x-foo"));
    QCOMPARE(e->priority,    5);
    QVERIFY(e->hidden);
    QVERIFY(!e->unsupported);
}

void TestKateSyntaxIndex::readHeader_doctypeWithEntities() {
    writeFile(xmlPath(QStringLiteral("ent.xml")),
              "<?xml version=\"1.0\"?>\n"
              "<!DOCTYPE language [\n"
              "  <!ENTITY ident \"[a-z]+\">\n"
              "]>\n"
              "<language name=\"Ent\" version=\"2\" kateversion=\"5.0\" section=\"Other\" "
              "extensions=\"*.ent\"><highlighting/></language>\n");
    const auto e = KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("ent.xml")));
    QVERIFY(e.has_value());
    QCOMPARE(e->name, QStringLiteral("Ent"));
}

void TestKateSyntaxIndex::readHeader_rejectsNonLanguage() {
    writeFile(xmlPath(QStringLiteral("other.xml")), "<?xml version=\"1.0\"?><catalog/>");
    QVERIFY(!KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("other.xml"))).has_value());

    writeFile(xmlPath(QStringLiteral("noname.xml")),
              "<?xml version=\"1.0\"?><language version=\"1\"/>");
    QVERIFY(!KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("noname.xml"))).has_value());

    QVERIFY(!KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("missing.xml"))).has_value());
}

void TestKateSyntaxIndex::readHeader_unsupportedKateversion() {
    writeFile(xmlPath(QStringLiteral("new.xml")),
              "<?xml version=\"1.0\"?>"
              "<language name=\"New\" version=\"1\" kateversion=\"99.0\" extensions=\"*.new\"/>");
    const auto e = KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("new.xml")));
    QVERIFY(e.has_value());
    QVERIFY(e->unsupported);

    // Exactly the supported version is fine.
    const QString sv = qce::kate::supportedSyntaxVersion().toString();
    writeFile(xmlPath(QStringLiteral("same.xml")),
              QStringLiteral("<?xml version=\"1.0\"?>"
                             "<language name=\"Same\" version=\"1\" kateversion=\"%1\"/>")
                  .arg(sv).toUtf8());
    QVERIFY(!KateSyntaxIndex::readHeader(xmlPath(QStringLiteral("same.xml")))->unsupported);
}

void TestKateSyntaxIndex::load_emptyDir_isDirtyAndEmpty() {
    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(idx.languages().isEmpty());
    QVERIFY(idx.isDirty());   // no index.json yet
    QVERIFY(idx.save());
    QVERIFY(QFile::exists(dataDir() + QStringLiteral("/index.json")));
    QVERIFY(!idx.isDirty());
}

void TestKateSyntaxIndex::saveAndLoad_roundTrip() {
    writeFile(xmlPath(QStringLiteral("a.xml")), langXml(QStringLiteral("A"), 1, QStringLiteral("*.a")));
    writeFile(xmlPath(QStringLiteral("b.xml")), langXml(QStringLiteral("B"), 2, QStringLiteral("*.b")));

    auto first = KateSyntaxIndex::load(dataDir());
    QCOMPARE(first.languages().size(), 2);
    QVERIFY(first.saveIfDirty());

    auto second = KateSyntaxIndex::load(dataDir());
    QVERIFY(!second.isDirty());
    QCOMPARE(second.languages(), first.languages());

    const QJsonObject root = [&] {
        QFile f(dataDir() + QStringLiteral("/index.json"));
        if (!f.open(QIODevice::ReadOnly)) return QJsonObject();
        return QJsonDocument::fromJson(f.readAll()).object();
    }();
    QCOMPARE(root.value(QStringLiteral("format")).toInt(), KateSyntaxIndex::kFormat);
    QCOMPARE(root.value(QStringLiteral("syntaxVersion")).toString(),
             qce::kate::supportedSyntaxVersion().toString());
}

void TestKateSyntaxIndex::refresh_unchangedFileIsNotReread() {
    const QString p = xmlPath(QStringLiteral("a.xml"));
    const QByteArray orig = langXml(QStringLiteral("A"), 1, QStringLiteral("*.a"));
    writeFile(p, orig);
    const QDateTime t = QDateTime::currentDateTime().addSecs(-3600);
    setMTime(p, t);
    QVERIFY(KateSyntaxIndex::load(dataDir()).save());

    // Same size and mtime, different content: the index must trust its cache.
    QByteArray garbage(orig.size(), 'x');
    writeFile(p, garbage);
    setMTime(p, t);

    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(!idx.isDirty());
    QVERIFY(idx.byName(QStringLiteral("A")) != nullptr);
}

void TestKateSyntaxIndex::refresh_changedFileIsReread() {
    const QString p = xmlPath(QStringLiteral("a.xml"));
    writeFile(p, langXml(QStringLiteral("A"), 1, QStringLiteral("*.a")));
    setMTime(p, QDateTime::currentDateTime().addSecs(-3600));
    QVERIFY(KateSyntaxIndex::load(dataDir()).save());

    writeFile(p, langXml(QStringLiteral("A"), 42, QStringLiteral("*.a")));
    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(idx.isDirty());
    QCOMPARE(idx.byName(QStringLiteral("A"))->version, 42);
}

void TestKateSyntaxIndex::refresh_removedAndAddedFiles() {
    writeFile(xmlPath(QStringLiteral("a.xml")), langXml(QStringLiteral("A"), 1, QStringLiteral("*.a")));
    writeFile(xmlPath(QStringLiteral("b.xml")), langXml(QStringLiteral("B"), 1, QStringLiteral("*.b")));
    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(idx.save());

    QVERIFY(QFile::remove(xmlPath(QStringLiteral("a.xml"))));
    writeFile(xmlPath(QStringLiteral("c.xml")), langXml(QStringLiteral("C"), 1, QStringLiteral("*.c")));

    QVERIFY(idx.refresh());
    QVERIFY(idx.isDirty());
    QVERIFY(idx.byFile(QStringLiteral("a.xml")) == nullptr);
    QVERIFY(idx.byName(QStringLiteral("A")) == nullptr);
    QVERIFY(idx.byName(QStringLiteral("B")) != nullptr);
    QVERIFY(idx.byName(QStringLiteral("C")) != nullptr);

    QVERIFY(idx.save());
    QVERIFY(!idx.refresh());   // nothing changed since
}

void TestKateSyntaxIndex::load_staleSyntaxVersion_rebuilds() {
    writeFile(xmlPath(QStringLiteral("a.xml")), langXml(QStringLiteral("A"), 1, QStringLiteral("*.a")));
    writeFile(dataDir() + QStringLiteral("/index.json"),
              R"({"format":1,"syntaxVersion":"5.256","languages":[
                   {"file":"ghost.xml","name":"Ghost","size":1,"mtime":1}]})");
    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(idx.isDirty());
    QVERIFY(idx.byName(QStringLiteral("Ghost")) == nullptr);
    QVERIFY(idx.byName(QStringLiteral("A")) != nullptr);
}

void TestKateSyntaxIndex::load_corruptJson_rebuilds() {
    writeFile(xmlPath(QStringLiteral("a.xml")), langXml(QStringLiteral("A"), 1, QStringLiteral("*.a")));
    writeFile(dataDir() + QStringLiteral("/index.json"), "{ not json");
    auto idx = KateSyntaxIndex::load(dataDir());
    QVERIFY(idx.isDirty());
    QCOMPARE(idx.languages().size(), 1);
}

void TestKateSyntaxIndex::byName_highestVersionWins() {
    writeFile(xmlPath(QStringLiteral("x1.xml")), langXml(QStringLiteral("X"), 3, QStringLiteral("*.x")));
    writeFile(xmlPath(QStringLiteral("x2.xml")), langXml(QStringLiteral("X"), 9, QStringLiteral("*.x")));
    auto idx = KateSyntaxIndex::load(dataDir());
    QCOMPARE(idx.byName(QStringLiteral("X"))->file, QStringLiteral("x2.xml"));
    QCOMPARE(idx.filePath(*idx.byName(QStringLiteral("X"))), xmlPath(QStringLiteral("x2.xml")));
}

void TestKateSyntaxIndex::forFileName_priorityAndUnsupported() {
    writeFile(xmlPath(QStringLiteral("c.xml")),
              langXml(QStringLiteral("C"), 1, QStringLiteral("*.c;*.h"), QStringLiteral("priority=\"5\"")));
    writeFile(xmlPath(QStringLiteral("cpp.xml")),
              langXml(QStringLiteral("C++"), 1, QStringLiteral("*.cpp; *.h"), QStringLiteral("priority=\"9\"")));
    writeFile(xmlPath(QStringLiteral("objc.xml")),
              langXml(QStringLiteral("ObjC"), 1, QStringLiteral("*.h;*.m")));
    writeFile(xmlPath(QStringLiteral("future.xml")),
              "<?xml version=\"1.0\"?><language name=\"Future\" version=\"1\" "
              "kateversion=\"99.0\" extensions=\"*.h\" priority=\"20\"/>");
    auto idx = KateSyntaxIndex::load(dataDir());

    auto names = [](const QList<const LanguageEntry*>& l) {
        QStringList r;
        for (auto* e : l) r << e->name;
        return r;
    };
    QCOMPARE(names(idx.forFileName(QStringLiteral("/some/dir/x.h"))),
             (QStringList{QStringLiteral("C++"), QStringLiteral("C"), QStringLiteral("ObjC")}));
    QCOMPARE(names(idx.forFileName(QStringLiteral("x.h"), /*includeUnsupported=*/true)),
             (QStringList{QStringLiteral("Future"), QStringLiteral("C++"), QStringLiteral("C"),
                          QStringLiteral("ObjC")}));
    QCOMPARE(names(idx.forFileName(QStringLiteral("main.cpp"))), QStringList{QStringLiteral("C++")});
    QVERIFY(idx.forFileName(QStringLiteral("README")).isEmpty());
    QVERIFY(idx.forFileName(QString()).isEmpty());
}

void TestKateSyntaxIndex::realData_matchesFileCount() {
    // Read-only smoke test over a real Kate data set, if one is installed.
    // Point QCE_KATE_TEST_DATA_DIR at a directory containing syntax/*.xml.
    const QString dir = qEnvironmentVariable("QCE_KATE_TEST_DATA_DIR");
    if (dir.isEmpty() || !QDir(dir + QStringLiteral("/syntax")).exists())
        QSKIP("QCE_KATE_TEST_DATA_DIR not set");

    const auto files = QDir(dir + QStringLiteral("/syntax"))
                           .entryList({QStringLiteral("*.xml")}, QDir::Files);
    QElapsedTimer t;
    t.start();
    auto idx = KateSyntaxIndex::load(dir);   // never saved: leave the real dir alone
    qInfo() << "indexed" << idx.languages().size() << "of" << files.size()
            << "files in" << t.elapsed() << "ms";
    QCOMPARE(idx.languages().size(), files.size());
    QVERIFY(idx.byName(QStringLiteral("C++")) != nullptr);
    QVERIFY(!idx.forFileName(QStringLiteral("main.cpp")).isEmpty());
    QCOMPARE(idx.forFileName(QStringLiteral("main.cpp")).first()->name, QStringLiteral("C++"));

    // C++ pulls in other languages via ##Name; resolve them through the index
    // and expect the same palette as the directory-scanning loader.
    const QString cpp = idx.filePath(*idx.byName(QStringLiteral("C++")));
    auto viaIndex = KateXmlReader::load(cpp, KateTheme{}, idx);
    auto viaScan  = KateXmlReader::load(cpp, KateTheme{});
    QVERIFY(viaIndex && viaScan);
    QCOMPARE(viaIndex->attributes().size(), viaScan->attributes().size());
}

QTEST_GUILESS_MAIN(TestKateSyntaxIndex)
#include "test_kate_syntax_index.moc"
