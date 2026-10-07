#include <qce/kate/KateXmlReader.h>
#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateTheme.h>

#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "KateTestPaths.h"

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
    void dtdCommentQuotes_doNotTerminateSubset();
    void dtdEntities_acceptBothQuotesAndHyphenatedNames();
    void realRubyXml_loadsOrSkips();
    void realDotXml_loadsOrSkips();
    void realCXml_loadsOrSkips();

    void fallthrough_switchesContextOnNoMatch();
    void multiPushSwitch_pushesAllContextsInOrder();
    void lookAheadCycle_terminates();
    void realYamlXml_keyValueDoesNotHang();
    void hlCHex_matchesHexLiteral();
    void hlCOct_matchesOctalLiteral();
    void hlCChar_matchesCharLiteral();
    void column_ruleFiresOnlyAtGivenColumn();
    void itemData_explicitColorOverridesTheme();
    void lineContinue_customChar();
    void crossLanguageInclude_resolvedViaSyntaxIndex();
};

// Helper: write src to a file in a temp dir and return the path.
static QString dumpToTemp(QTemporaryDir& dir, const QString& fileName,
                           const char* src) {
    const QString path = dir.filePath(fileName);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};  // load() then fails
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

void TestKateXmlReader::dtdCommentQuotes_doNotTerminateSubset() {
    // Like ruby.xml: an apostrophe in a DTD comment must not open a quote.
    static const char* xml = R"(<?xml version="1.0"?>
<!DOCTYPE language [
    <!ENTITY num "[0-9]+">
    <!-- doesn't apply to constants; quoted brackets follow -->
    <!ENTITY quote "[']">
    <!ENTITY digits "&num;">
]>
<language name="comment-dtd">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <RegExpr attribute="Number" String="&digits;"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Number" defStyleNum="dsDecVal"/>
  </itemDatas>
</highlighting>
</language>)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("comment.xml"), xml));
    QVERIFY(hl);
    const auto initial = hl->initialState();
    qce::HighlightState end;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("abc 123"), initial, spans, end);
    bool numberFound = false;
    for (const auto& span : spans) {
        if (span.attributeId == 1 && span.start == 4 && span.length == 3) {
            numberFound = true;
        }
    }
    QVERIFY(numberFound);
}

void TestKateXmlReader::dtdEntities_acceptBothQuotesAndHyphenatedNames() {
    static const char* xml = R"(<?xml version="1.0"?>
<!DOCTYPE language [
    <!ENTITY num "[0-9]+">
    <!ENTITY commands-heads "&num;">
    <!-- <!ENTITY commands-heads "bad"> -->
    <!ENTITY quoted '"&commands-heads;"'>
    <!ENTITY token-name '&lt;&amp;"&apos;'>
    <!ENTITY single-quote "x'y">
]>
<language name="quoted-dtd">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <RegExpr attribute="Number" String="&quoted;"/>
      <StringDetect attribute="String" String="&token-name;"/>
      <StringDetect attribute="String" String='&single-quote;'/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Number" defStyleNum="dsDecVal"/>
    <itemData name="String" defStyleNum="dsString"/>
  </itemDatas>
</highlighting>
</language>)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("quotes.xml"), xml));
    QVERIFY(hl);
    qce::HighlightState end;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("\"123\" <&\"' x'y"), hl->initialState(), spans, end);
    bool numberFound = false;
    bool doubleQuoteFound = false;
    bool singleQuoteFound = false;
    for (const auto& span : spans) {
        if (span.attributeId == 1 && span.start == 0 && span.length == 5) {
            numberFound = true;
        }
        if (span.attributeId == 2 && span.start == 6 && span.length == 4) {
            doubleQuoteFound = true;
        }
        if (span.attributeId == 2 && span.start == 11 && span.length == 3) {
            singleQuoteFound = true;
        }
    }
    QVERIFY(numberFound);
    QVERIFY(doubleQuoteFound);
    QVERIFY(singleQuoteFound);
}

void TestKateXmlReader::realRubyXml_loadsOrSkips() {
    const QString path = kateSyntaxPath(QStringLiteral("ruby.xml"));
    if (path.isEmpty()) QSKIP("ruby.xml not installed on this system");
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl);
    qce::HighlightState end;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("puts 123"), hl->initialState(), spans, end);
    QVERIFY(!spans.isEmpty());
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

// ---------------------------------------------------------------------------
// fallthrough
// ---------------------------------------------------------------------------
void TestKateXmlReader::fallthrough_switchesContextOnNoMatch() {
    // Normal context has no rules but fallthrough → Fallback.
    // Fallback context has a DetectChar rule that highlights '#' red.
    // For text "ab#cd": 'a','b','d' should get default attr; '#' should get Red.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="ft" version="1" extensions="*.ft" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay"
             fallthrough="true" fallthroughContext="Fallback"/>
    <context name="Fallback" attribute="Normal" lineEndContext="#stay">
      <DetectChar attribute="Red" context="#stay" char="#"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Red"    defStyleNum="dsAlert"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("ft.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("ab#cd"), s, spans, sOut);

    // attribute index 1 = "Red" (dsAlert)
    auto attrAt = [&](int col) {
        for (const auto& sp : spans)
            if (sp.start <= col && col < sp.start + sp.length) return sp.attributeId;
        return -1;
    };
    QCOMPARE(attrAt(2), 1); // '#' → Red
    QVERIFY(attrAt(0) != 1); // 'a' → not Red
    QVERIFY(attrAt(3) != 1); // 'c' → not Red
}

// ---------------------------------------------------------------------------
// Context switches
// ---------------------------------------------------------------------------
void TestKateXmlReader::multiPushSwitch_pushesAllContextsInOrder() {
    // context="A!B" pushes A and then B; "#pop" from B returns to A, not to
    // Normal. Before multi-push support "A!B" was looked up as one name.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="mp" version="1" extensions="*.mp" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <DetectChar attribute="Normal" context="A!B" char="x"/>
    </context>
    <context name="A" attribute="Normal" lineEndContext="#stay">
      <DetectChar attribute="Red" context="#stay" char="a"/>
    </context>
    <context name="B" attribute="Normal" lineEndContext="#stay">
      <DetectChar attribute="Normal" context="#pop" char="#"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Red"    defStyleNum="dsAlert"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("mp.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("x"), hl->initialState(), spans, sOut);
    QCOMPARE(sOut.contextStack.size(), 3);  // Normal, A, B

    hl->highlightLine(QStringLiteral("x#a"), hl->initialState(), spans, sOut);
    QCOMPARE(sOut.contextStack.size(), 2);  // B popped, A on top
    QCOMPARE(spans.last().attributeId, 1);  // 'a' highlighted by A
}

void TestKateXmlReader::lookAheadCycle_terminates() {
    // Two lookAhead rules switching back and forth never consume anything.
    // The highlighter must still finish the line instead of hanging.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="cyc" version="1" extensions="*.cyc" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <DetectChar lookAhead="1" context="Other" char="a"/>
    </context>
    <context name="Other" attribute="Normal" lineEndContext="#stay">
      <DetectChar lookAhead="1" context="#pop" char="a"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("cyc.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("aaa"), hl->initialState(), spans, sOut);
    int covered = 0;
    for (const auto& sp : spans) covered += sp.length;
    QCOMPARE(covered, 3);
}

void TestKateXmlReader::realYamlXml_keyValueDoesNotHang() {
    // yaml.xml switches with context="BlockPrefix!ValueTextIndent!…" from a
    // lookAhead rule at column 0; any "key: value" line used to hang.
    const QString path = kateSyntaxPath(QStringLiteral("yaml.xml"));
    if (path.isEmpty()) QSKIP("yaml.xml not installed on this system");
    auto hl = KateXmlReader::load(path);
    QVERIFY(hl);

    const QStringList lines = {
        QStringLiteral("# comment"), QStringLiteral("---"),
        QStringLiteral("Language: Cpp"), QStringLiteral("BraceWrapping: "),
        QStringLiteral("  AfterClass: false"), QStringLiteral("IndentWidth: 4"),
    };
    qce::HighlightState s = hl->initialState();
    for (const QString& line : lines) {
        qce::HighlightState sOut;
        QVector<qce::StyleSpan> spans;
        hl->highlightLine(line, s, spans, sOut);
        QVERIFY2(!spans.isEmpty(), qPrintable(line));
        s = sOut;
    }

    // The key and the value are highlighted differently
    qce::HighlightState sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("Language: Cpp"), hl->initialState(), spans, sOut);
    auto attrAt = [&](int col) {
        for (const auto& sp : spans)
            if (sp.start <= col && col < sp.start + sp.length) return sp.attributeId;
        return -1;
    };
    QVERIFY(attrAt(0) != attrAt(10));
}

// ---------------------------------------------------------------------------
// HlCHex / HlCOct / HlCChar
// ---------------------------------------------------------------------------
void TestKateXmlReader::hlCHex_matchesHexLiteral() {
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="hex" version="1" extensions="*.hex" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <HlCHex attribute="Hex" context="#stay"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Hex"    defStyleNum="dsBaseN"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("hex.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("x 0xFF y"), s, spans, sOut);

    // "0xFF" starts at col 2, length 4; attributeId 1 = Hex
    bool found = false;
    for (const auto& sp : spans)
        if (sp.attributeId == 1 && sp.start == 2 && sp.length == 4) found = true;
    QVERIFY(found);
}

void TestKateXmlReader::hlCOct_matchesOctalLiteral() {
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="oct" version="1" extensions="*.oct" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <HlCOct attribute="Oct" context="#stay"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Oct"    defStyleNum="dsBaseN"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("oct.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    hl->highlightLine(QStringLiteral("a 0755 b"), s, spans, sOut);

    // "0755" starts at col 2, length 4; attributeId 1 = Oct
    bool found = false;
    for (const auto& sp : spans)
        if (sp.attributeId == 1 && sp.start == 2 && sp.length == 4) found = true;
    QVERIFY(found);
}

void TestKateXmlReader::hlCChar_matchesCharLiteral() {
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="chr" version="1" extensions="*.chr" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <HlCChar attribute="Chr" context="#stay"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Chr"    defStyleNum="dsChar"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("chr.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    // 'a' starts at col 0, length 3
    hl->highlightLine(QStringLiteral("'a' x"), s, spans, sOut);

    bool found = false;
    for (const auto& sp : spans)
        if (sp.attributeId == 1 && sp.start == 0 && sp.length == 3) found = true;
    QVERIFY(found);
}

// ---------------------------------------------------------------------------
// column constraint
// ---------------------------------------------------------------------------
void TestKateXmlReader::column_ruleFiresOnlyAtGivenColumn() {
    // DetectChar for '#' with column="3" — should only match at position 3.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="col" version="1" extensions="*.col" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <DetectChar attribute="Hash" context="#stay" char="#" column="3"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Hash"   defStyleNum="dsAlert"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("col.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;
    // '#' at col 0 must NOT match; '#' at col 3 must match.
    hl->highlightLine(QStringLiteral("#ab#"), s, spans, sOut);

    auto attrAt = [&](int col) {
        for (const auto& sp : spans)
            if (sp.start <= col && col < sp.start + sp.length) return sp.attributeId;
        return -1;
    };
    QVERIFY(attrAt(0) != 1); // '#' at col 0 — no match (column constraint)
    QCOMPARE(attrAt(3), 1);  // '#' at col 3 — matches
}

// ---------------------------------------------------------------------------
// itemData explicit color
// ---------------------------------------------------------------------------
void TestKateXmlReader::itemData_explicitColorOverridesTheme() {
    // itemData with color="#FF0000" — should override the dsNormal black.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="clr" version="1" extensions="*.clr" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Red" lineEndContext="#stay"/>
  </contexts>
  <itemDatas>
    <itemData name="Red" defStyleNum="dsNormal" color="#FF0000"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("clr.xml"), xml));
    QVERIFY(hl);

    QVERIFY(!hl->attributes().isEmpty());
    const QColor fg = hl->attributes().first().foreground;
    QCOMPARE(fg.red(),   255);
    QCOMPARE(fg.green(),   0);
    QCOMPARE(fg.blue(),    0);
}

// ---------------------------------------------------------------------------
// LineContinue custom char
// ---------------------------------------------------------------------------
void TestKateXmlReader::lineContinue_customChar() {
    // LineContinue with char="&amp;" (i.e. '&') instead of default '\'.
    static const char* xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="lc" version="1" extensions="*.lc" section="Other">
<highlighting>
  <contexts>
    <context name="Normal" attribute="Normal" lineEndContext="#stay">
      <LineContinue attribute="Cont" context="#stay" char="&amp;"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
    <itemData name="Cont"   defStyleNum="dsSpecialChar"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    auto hl = KateXmlReader::load(dumpToTemp(dir, QStringLiteral("lc.xml"), xml));
    QVERIFY(hl);

    qce::HighlightState s = hl->initialState(), sOut;
    QVector<qce::StyleSpan> spans;

    // Line ending with '&' → should match; ending with '\' → should not.
    hl->highlightLine(QStringLiteral("abc&"), s, spans, sOut);
    bool contFound = false;
    for (const auto& sp : spans)
        if (sp.attributeId == 1) contFound = true;
    QVERIFY(contFound);

    spans.clear();
    hl->highlightLine(QStringLiteral("abc\\"), s, spans, sOut);
    bool backslashMatched = false;
    for (const auto& sp : spans)
        if (sp.attributeId == 1) backslashMatched = true;
    QVERIFY(!backslashMatched);
}

// ---------------------------------------------------------------------------
// ##Lang includes resolved through a KateSyntaxIndex rather than a scan of the
// main file's directory. host.xml lives outside the indexed syntax dir, so
// the plain load() cannot find Guest; the index-aware overload can.
// ---------------------------------------------------------------------------
void TestKateXmlReader::crossLanguageInclude_resolvedViaSyntaxIndex() {
    static const char* kHost = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="Host" version="1" kateversion="5.0" extensions="*.host" section="Other">
<highlighting>
  <contexts>
    <context attribute="Normal" lineEndContext="#stay" name="Normal">
      <IncludeRules context="##Guest"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal" defStyleNum="dsNormal"/>
  </itemDatas>
</highlighting>
</language>
)";
    static const char* kGuest = R"(<?xml version="1.0" encoding="UTF-8"?>
<language name="Guest" version="1" kateversion="5.0" extensions="*.guest" section="Other">
<highlighting>
  <list name="kws"><item>zap</item></list>
  <contexts>
    <context attribute="Normal" lineEndContext="#stay" name="Normal">
      <keyword attribute="Keyword" context="#stay" String="kws"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Normal"  defStyleNum="dsNormal"/>
    <itemData name="Keyword" defStyleNum="dsKeyword"/>
  </itemDatas>
</highlighting>
</language>
)";
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("data/syntax")));
    const QString hostPath = dumpToTemp(dir, QStringLiteral("host.xml"), kHost);
    dumpToTemp(dir, QStringLiteral("data/syntax/guest.xml"), kGuest);

    auto keywordSpans = [](const qce::RulesHighlighter& hl) {
        qce::HighlightState s = hl.initialState(), sOut;
        QVector<qce::StyleSpan> spans;
        hl.highlightLine(QStringLiteral("a zap b"), s, spans, sOut);
        int n = 0;
        for (const auto& sp : spans)
            if (sp.start == 2 && sp.length == 3 && sp.attributeId > 0) ++n;
        return n;
    };

    // Without the index: Guest is not next to host.xml → include unresolved.
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression(QStringLiteral("language not found in syntax directory")));
    auto plain = KateXmlReader::load(hostPath);
    QVERIFY(plain);
    QCOMPARE(keywordSpans(*plain), 0);

    // With the index over data/: Guest found, "zap" highlighted.
    const auto index = qce::kate::KateSyntaxIndex::load(dir.filePath(QStringLiteral("data")));
    QVERIFY(index.byName(QStringLiteral("Guest")));
    auto viaIndex = KateXmlReader::load(hostPath, KateTheme{}, index);
    QVERIFY(viaIndex);
    QCOMPARE(keywordSpans(*viaIndex), 1);
}

QTEST_APPLESS_MAIN(TestKateXmlReader)
#include "test_kate_xml_reader.moc"
