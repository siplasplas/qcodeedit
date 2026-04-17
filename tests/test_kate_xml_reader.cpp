#include "../demo/KateXmlReader.h"

#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

/// Tiny Kate-style XML that exercises the main reader paths:
/// - <itemDatas> with known defStyleNum
/// - <list>/<item>
/// - <contexts>/<context> with DetectChar, Detect2Chars, keyword, WordDetect
/// - context switches: Name, #pop, #stay, lineEndContext="#pop"
static const char* kMinimalXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="mini" version="1" extensions="*.mini" section="Scripts">
<highlighting>
  <list name="kws">
    <item>if</item>
    <item>else</item>
    <item>return</item>
  </list>
  <contexts>
    <context attribute="Normal" lineEndContext="#stay" name="Normal">
      <Detect2Chars attribute="Comment" context="LineComment" char="/" char1="/"/>
      <Detect2Chars attribute="Comment" context="BlockComment" char="/" char1="*"/>
      <DetectChar   attribute="String"  context="String"       char="&quot;"/>
      <keyword      attribute="Keyword" context="#stay"        String="kws"/>
      <Int          attribute="Number"  context="#stay"/>
    </context>
    <context attribute="Comment" lineEndContext="#pop" name="LineComment"/>
    <context attribute="Comment" lineEndContext="#stay" name="BlockComment">
      <Detect2Chars attribute="Comment" context="#pop" char="*" char1="/"/>
    </context>
    <context attribute="String" lineEndContext="#stay" name="String">
      <HlCStringChar attribute="StringChar"/>
      <DetectChar    attribute="String" context="#pop" char="&quot;"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal"     defStyleNum="dsNormal"/>
    <itemData name="Keyword"    defStyleNum="dsKeyword"/>
    <itemData name="String"     defStyleNum="dsString"/>
    <itemData name="StringChar" defStyleNum="dsSpecialChar"/>
    <itemData name="Comment"    defStyleNum="dsComment"/>
    <itemData name="Number"     defStyleNum="dsDecVal"/>
  </itemDatas>
</highlighting>
<general>
  <keywords casesensitive="1"/>
</general>
</language>
)";

class TestKateXmlReader : public QObject {
    Q_OBJECT
private slots:
    void readsMinimalDefinition();
    void highlightsKeyword();
    void multiLineCommentPropagatesState();
    void handlesDtdEntities();
    void realDotXml_loadsOrSkips();
    void realCXml_loadsOrSkips();
};

// Helper: write src to a file in a temp dir and return the path.
static QString dumpToTemp(QTemporaryDir& dir, const QString& fileName,
                           const char* src) {
    const QString path = dir.filePath(fileName);
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Text);
    f.write(src);
    f.close();
    return path;
}

void TestKateXmlReader::readsMinimalDefinition() {
    QTemporaryDir dir;
    const QString path = dumpToTemp(dir, QStringLiteral("mini.xml"), kMinimalXml);

    auto hl = KateXmlReader::load(path);
    QVERIFY(hl != nullptr);
    // 6 itemDatas → 6 palette entries.
    QCOMPARE(hl->attributes().size(), 6);
    // Initial state points to the first context (Normal).
    QCOMPARE(hl->initialState().contextStack.size(), 1);
}

void TestKateXmlReader::highlightsKeyword() {
    QTemporaryDir dir;
    const QString path = dumpToTemp(dir, QStringLiteral("mini.xml"), kMinimalXml);
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("if x return 5"), s, spans, sOut);
    // Expect: 'if' (keyword), ' ', 'x' (default), ' ', 'return' (keyword), ' ',
    //         '5' (number) — at least both keywords appear as distinct spans
    //         with the Keyword attribute.
    int kwSpans = 0;
    for (const auto& sp : spans) {
        // Keyword attributeId depends on <itemDatas> order: Keyword = index 1.
        if (sp.attributeId == 1) ++kwSpans;
    }
    QCOMPARE(kwSpans, 2);
}

void TestKateXmlReader::multiLineCommentPropagatesState() {
    QTemporaryDir dir;
    const QString path = dumpToTemp(dir, QStringLiteral("mini.xml"), kMinimalXml);
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl);

    qce::HighlightState s0 = hl->initialState(), s1, s2;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("x /* open"), s0, spans, s1);
    // Still inside the comment at end of line → state differs from initial.
    QVERIFY(s1 != s0);
    spans.clear();
    hl->highlightLine(QStringLiteral("still comment */ done"), s1, spans, s2);
    // Back to Normal.
    QCOMPARE(s2, s0);
}

void TestKateXmlReader::handlesDtdEntities() {
    // Inline DTD with &num; entity used inside a RegExpr.
    static const char* xmlWithDtd = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE language [
    <!ENTITY num "[0-9]+">
]>
<language name="mini2" version="1" extensions="*.mini2" section="Scripts">
<highlighting>
  <contexts>
    <context attribute="Normal" lineEndContext="#stay" name="Normal">
      <RegExpr attribute="Number" String="&num;"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Number" defStyleNum="dsDecVal"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    const QString path = dumpToTemp(dir, QStringLiteral("mini2.xml"), xmlWithDtd);
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("abc 123 def"), s, spans, sOut);
    // The regex [0-9]+ should have produced a Number span.
    bool numberFound = false;
    for (const auto& sp : spans) {
        if (sp.attributeId == 1 && sp.length == 3 && sp.start == 4) numberFound = true;
    }
    QVERIFY(numberFound);
}

static QString kateSyntaxPath(const QString& fileName) {
    const QString p = QDir::homePath()
        + QStringLiteral("/.local/share/org.kde.syntax-highlighting/syntax/")
        + fileName;
    return QFile::exists(p) ? p : QString();
}

void TestKateXmlReader::realDotXml_loadsOrSkips() {
    const QString path = kateSyntaxPath(QStringLiteral("dot.xml"));
    if (path.isEmpty()) QSKIP("dot.xml not installed on this system");
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl != nullptr);
    QVERIFY(hl->attributes().size() > 0);
    // Try a short highlight pass to make sure it doesn't crash.
    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("digraph G { a -> b; }"), s, spans, sOut);
    QVERIFY(!spans.isEmpty());
}

void TestKateXmlReader::realCXml_loadsOrSkips() {
    const QString path = kateSyntaxPath(QStringLiteral("c.xml"));
    if (path.isEmpty()) QSKIP("c.xml not installed on this system");
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl != nullptr);
    QVERIFY(hl->attributes().size() > 0);
    // c.xml exercises DTD entities (&int; etc.); just make sure no crash.
    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("int main() { return 0; }"), s, spans, sOut);
    QVERIFY(!spans.isEmpty());
}

QTEST_APPLESS_MAIN(TestKateXmlReader)
#include "test_kate_xml_reader.moc"
