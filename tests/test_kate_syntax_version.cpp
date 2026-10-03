#include <qce/kate/KateSyntaxVersion.h>

#include <QtTest/QtTest>

using qce::kate::SyntaxVersion;

class TestKateSyntaxVersion : public QObject {
    Q_OBJECT

private slots:
    void supported_is631();
    void toString();
    void updateFileNameAndUrl();
    void ordering_isNumericNotLexical();
    void parse_valid();
    void parse_invalid();
};

void TestKateSyntaxVersion::supported_is631() {
    constexpr SyntaxVersion v = qce::kate::supportedSyntaxVersion();
    static_assert(v.major == 6 && v.minor == 31);
    QCOMPARE(v.toString(), QStringLiteral("6.31"));
}

void TestKateSyntaxVersion::toString() {
    QCOMPARE((SyntaxVersion{5, 256}).toString(), QStringLiteral("5.256"));
    QCOMPARE((SyntaxVersion{6, 0}).toString(),   QStringLiteral("6.0"));
}

void TestKateSyntaxVersion::updateFileNameAndUrl() {
    const SyntaxVersion v{6, 31};
    QCOMPARE(v.updateFileName(), QStringLiteral("update-6.31.xml"));
    QCOMPARE(v.updateUrl(),
             QStringLiteral("https://kate-editor.org/syntax/update-6.31.xml"));
}

void TestKateSyntaxVersion::ordering_isNumericNotLexical() {
    static_assert(SyntaxVersion{6, 9} < SyntaxVersion{6, 31});
    static_assert(SyntaxVersion{5, 256} < SyntaxVersion{6, 0});
    static_assert(SyntaxVersion{6, 31} == SyntaxVersion{6, 31});
    static_assert(SyntaxVersion{6, 32} > SyntaxVersion{6, 31});
    QVERIFY(true);
}

void TestKateSyntaxVersion::parse_valid() {
    QCOMPARE(SyntaxVersion::parse(u"6.31"),   std::optional(SyntaxVersion{6, 31}));
    QCOMPARE(SyntaxVersion::parse(u"5.0"),    std::optional(SyntaxVersion{5, 0}));
    QCOMPARE(SyntaxVersion::parse(u" 5.62 "), std::optional(SyntaxVersion{5, 62}));
    QCOMPARE(SyntaxVersion::parse(u"3"),      std::optional(SyntaxVersion{3, 0}));
}

void TestKateSyntaxVersion::parse_invalid() {
    QVERIFY(!SyntaxVersion::parse(u"").has_value());
    QVERIFY(!SyntaxVersion::parse(u".").has_value());
    QVERIFY(!SyntaxVersion::parse(u"6.").has_value());
    QVERIFY(!SyntaxVersion::parse(u".31").has_value());
    QVERIFY(!SyntaxVersion::parse(u"6.31.1").has_value());
    QVERIFY(!SyntaxVersion::parse(u"-6.31").has_value());
    QVERIFY(!SyntaxVersion::parse(u"6.x").has_value());
}

QTEST_GUILESS_MAIN(TestKateSyntaxVersion)
#include "test_kate_syntax_version.moc"
