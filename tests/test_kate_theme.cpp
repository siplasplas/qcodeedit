#include <qce/kate/KatePaths.h>
#include <qce/kate/KateTheme.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

// KateTheme editor colours and the default-theme choice (Breeze Light/Dark
// with fallbacks), against theme files in a temporary data directory.
class TestKateTheme : public QObject {
    Q_OBJECT

private:
    static void writeTheme(const QString& path, const QString& name, const QString& bg) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral(R"({
  "metadata": { "name": "%1" },
  "editor-colors": {
    "BackgroundColor": "%2",
    "IconBorder": "#f0f0f0",
    "LineNumbers": "#a0a0a0",
    "Separator": "#d5d5d5"
  },
  "text-styles": { "Normal": { "text-color": "#1f1c1b" } }
})").arg(name, bg).toUtf8());
    }

    QTemporaryDir m_dir;

    QString themesDir() const { return m_dir.path() + QStringLiteral("/themes"); }

private slots:
    void init() {
        QVERIFY(m_dir.isValid());
        QDir(themesDir()).removeRecursively();
        QVERIFY(QDir().mkpath(themesDir()));
        qce::kate::setDataDirOverride(m_dir.path());
    }

    void cleanupTestCase() { qce::kate::setDataDirOverride({}); }

    void readsGutterColours() {
        const QString path = themesDir() + QStringLiteral("/t.theme");
        writeTheme(path, QStringLiteral("T"), QStringLiteral("#ffffff"));
        const KateTheme t = KateTheme::load(path);
        QVERIFY(t.isValid());
        QCOMPARE(t.editorBackground, QColor("#ffffff"));
        QCOMPARE(t.iconBorder, QColor("#f0f0f0"));
        QCOMPARE(t.lineNumbers, QColor("#a0a0a0"));
        QCOMPARE(t.separator, QColor("#d5d5d5"));
    }

    void missingEditorColoursAreInvalid() {
        const QString path = themesDir() + QStringLiteral("/bare.theme");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({ "metadata": { "name": "Bare" } })");
        f.close();
        const KateTheme t = KateTheme::load(path);
        QVERIFY(t.isValid());
        QVERIFY(!t.iconBorder.isValid());
        QVERIFY(!t.lineNumbers.isValid());
        QVERIFY(!t.separator.isValid());
    }

    void defaultPathIsBreezeInThemesDir() {
        const QString path = KateTheme::defaultThemePath();
        QVERIFY(path == themesDir() + QStringLiteral("/breeze-light.theme")
             || path == themesDir() + QStringLiteral("/breeze-dark.theme"));
    }

    void defaultMatchesDesktopWhenBothExist() {
        writeTheme(themesDir() + QStringLiteral("/breeze-light.theme"),
                   QStringLiteral("Breeze Light"), QStringLiteral("#ffffff"));
        writeTheme(themesDir() + QStringLiteral("/breeze-dark.theme"),
                   QStringLiteral("Breeze Dark"), QStringLiteral("#232629"));
        const bool dark = KateTheme::defaultThemePath().endsWith(QStringLiteral("breeze-dark.theme"));
        QCOMPARE(KateTheme::loadDefault().name,
                 dark ? QStringLiteral("Breeze Dark") : QStringLiteral("Breeze Light"));
    }

    void defaultFallsBackToBreezeLight() {
        writeTheme(themesDir() + QStringLiteral("/breeze-light.theme"),
                   QStringLiteral("Breeze Light"), QStringLiteral("#ffffff"));
        QCOMPARE(KateTheme::loadDefault().name, QStringLiteral("Breeze Light"));
    }

    void defaultIsInvalidWithoutBreeze() {
        writeTheme(themesDir() + QStringLiteral("/other.theme"),
                   QStringLiteral("Other"), QStringLiteral("#000000"));
        QVERIFY(!KateTheme::loadDefault().isValid());
    }
};

QTEST_MAIN(TestKateTheme)
#include "test_kate_theme.moc"
