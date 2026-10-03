#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxVersion.h>

#include <QStandardPaths>
#include <QtTest/QtTest>

class TestKatePaths : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void dataRoot_isGenericDataLocationPlusQcodeedit();
    void dataDir_defaultHasVersionSuffix();
    void syntaxAndThemes_areUnderDataDir();
    void env_overridesDefault();
    void api_overridesEnv();
    void override_cleared_fallsBack();
    void override_isCleaned();
};

void TestKatePaths::initTestCase() {
    // Redirect GenericDataLocation to a test location (~/.qttest/share on Linux).
    QStandardPaths::setTestModeEnabled(true);
}

void TestKatePaths::init() {
    qunsetenv("QCE_KATE_DATA_DIR");
    qce::kate::setDataDirOverride({});
}

void TestKatePaths::cleanup() {
    init();
}

void TestKatePaths::dataRoot_isGenericDataLocationPlusQcodeedit() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QVERIFY(!base.isEmpty());
    QCOMPARE(qce::kate::dataRoot(), base + QStringLiteral("/qcodeedit"));
}

void TestKatePaths::dataDir_defaultHasVersionSuffix() {
    const QString expected = qce::kate::dataRoot() + QStringLiteral("/kate-")
        + qce::kate::supportedSyntaxVersion().toString();
    QCOMPARE(qce::kate::dataDir(), expected);
    QVERIFY(qce::kate::dataDir().endsWith(QStringLiteral("/qcodeedit/kate-6.31")));
}

void TestKatePaths::syntaxAndThemes_areUnderDataDir() {
    QCOMPARE(qce::kate::syntaxDir(), qce::kate::dataDir() + QStringLiteral("/syntax"));
    QCOMPARE(qce::kate::themesDir(), qce::kate::dataDir() + QStringLiteral("/themes"));
}

void TestKatePaths::env_overridesDefault() {
    qputenv("QCE_KATE_DATA_DIR", "/tmp/qce-env-data");
    QCOMPARE(qce::kate::dataDir(),   QStringLiteral("/tmp/qce-env-data"));
    QCOMPARE(qce::kate::syntaxDir(), QStringLiteral("/tmp/qce-env-data/syntax"));
}

void TestKatePaths::api_overridesEnv() {
    qputenv("QCE_KATE_DATA_DIR", "/tmp/qce-env-data");
    qce::kate::setDataDirOverride(QStringLiteral("/tmp/qce-api-data"));
    QCOMPARE(qce::kate::dataDir(),   QStringLiteral("/tmp/qce-api-data"));
    QCOMPARE(qce::kate::themesDir(), QStringLiteral("/tmp/qce-api-data/themes"));
}

void TestKatePaths::override_cleared_fallsBack() {
    const QString def = qce::kate::dataDir();
    qce::kate::setDataDirOverride(QStringLiteral("/tmp/qce-api-data"));
    qce::kate::setDataDirOverride({});
    QCOMPARE(qce::kate::dataDir(), def);
}

void TestKatePaths::override_isCleaned() {
    qce::kate::setDataDirOverride(QStringLiteral("/tmp//qce-api-data/"));
    QCOMPARE(qce::kate::dataDir(),   QStringLiteral("/tmp/qce-api-data"));
    QCOMPARE(qce::kate::syntaxDir(), QStringLiteral("/tmp/qce-api-data/syntax"));
}

QTEST_GUILESS_MAIN(TestKatePaths)
#include "test_kate_paths.moc"
