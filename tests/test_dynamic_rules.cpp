#include <qce/kate/KateXmlReader.h>

#include <qce/HighlightState.h>
#include <qce/IHighlighter.h>
#include <qce/RulesHighlighter.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace qce;

/// Minimal Kate-style XML exercising the dynamic="true" path with the same
/// idiom as the real xml.xml: a RegExpr with a look-ahead capture group
/// ("foo" after "<") pushes a short-lived context whose single StringDetect
/// has String="%1" dynamic="true" and pops back immediately. End tags use
/// the same idiom after "</".
static const char* kDynamicXml = R"xml(<?xml version="1.0" encoding="UTF-8"?>
<language name="dyn" version="1" extensions="*.dyn" section="Test">
<highlighting>
  <contexts>
    <context attribute="Text" lineEndContext="#stay" name="Normal">
      <RegExpr  attribute="Sym"     context="ElName"    String="&lt;(?=([A-Za-z]+))" />
      <RegExpr  attribute="Sym"     context="ElEndName" String="&lt;/(?=([A-Za-z]+))" />
      <DetectChar attribute="Sym"   context="#stay"     char="&gt;"/>
    </context>
    <context attribute="Text" lineEndContext="#pop" name="ElName">
      <StringDetect attribute="Elem" context="#pop" String="%1" dynamic="true"/>
    </context>
    <context attribute="Text" lineEndContext="#pop" name="ElEndName">
      <StringDetect attribute="Elem" context="#pop" String="%1" dynamic="true"/>
    </context>
  </contexts>
  <itemDatas>
    <itemData name="Text" defStyleNum="dsNormal"/>
    <itemData name="Sym"  defStyleNum="dsNormal"/>
    <itemData name="Elem" defStyleNum="dsKeyword"/>
  </itemDatas>
</highlighting>
<general>
  <keywords casesensitive="1"/>
</general>
</language>
)xml";

static std::unique_ptr<qce::RulesHighlighter> loadDynamicHighlighter(QTemporaryDir& dir) {
    const QString path = dir.path() + QStringLiteral("/dyn.xml");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return nullptr;  // callers check the result
    f.write(kDynamicXml);
    f.close();
    return KateXmlReader::load(path);
}

/// Returns the attribute id for an itemData name, or -1 if not present.
/// The loader preserves <itemData> order, so name → id is deterministic.
/// Callers must pass the names in XML order.
static int attrIdByIndex(const qce::RulesHighlighter* hl, int index) {
    return index < hl->attributes().size() ? index : -1;
}

class TestDynamicRules : public QObject {
    Q_OBJECT
private slots:
    void tagPair_bothNamesRenderAsElem();             // §4.1
    void unrelatedCloser_doesNotEatNameUnder();       // §4.2
    void captureDepth_innerContextDoesNotPollute();   // §4.3
    void stateEquality_capturesDistinguish();         // §4.4
};

/// §4.1 — Tokenise <foo>hello</foo>; "foo" in both opener and closer
/// must render with the Elem attribute.
void TestDynamicRules::tagPair_bothNamesRenderAsElem() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto hl = loadDynamicHighlighter(tmp);
    QVERIFY(hl != nullptr);

    const int attrElem = attrIdByIndex(hl.get(), 2);   // "Elem"

    const QString line = QStringLiteral("<foo>hello</foo>");
    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> spans;
    hl->highlightLine(line, s0, spans, s1);

    // Locate the "foo" spans by byte offset and attribute.
    int openerElem = -1, closerElem = -1;
    for (const auto& sp : spans) {
        if (sp.attributeId != attrElem || sp.length != 3) continue;
        if (sp.start == 1)  openerElem = sp.start;   // after '<'
        if (sp.start == 12) closerElem = sp.start;   // after '</'
    }
    QCOMPARE(openerElem, 1);
    QCOMPARE(closerElem, 12);
}

/// §4.2 — For malformed "<a><b></a></b>", the closer-name context
/// consumes whatever literal name follows "</"; the rule must not
/// retroactively match "a" over "b". Verified by checking that each
/// end-tag name is marked Elem by its OWN capture, not by earlier ones.
void TestDynamicRules::unrelatedCloser_doesNotEatNameUnder() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    auto hl = loadDynamicHighlighter(tmp);
    QVERIFY(hl != nullptr);

    const int attrElem = attrIdByIndex(hl.get(), 2);

    // "<a><b></a></b>" — offsets:
    //  '<' a '>' '<' b '>' '<' '/' a '>' '<' '/' b '>'
    //   0  1  2   3  4  5   6  7  8  9  10 11 12 13
    const QString line = QStringLiteral("<a><b></a></b>");
    HighlightState s0 = hl->initialState(), s1;
    QVector<StyleSpan> spans;
    hl->highlightLine(line, s0, spans, s1);

    bool openerA = false, openerB = false, closerA = false, closerB = false;
    for (const auto& sp : spans) {
        if (sp.attributeId != attrElem || sp.length != 1) continue;
        if (sp.start == 1)  openerA = true;   // 'a' in <a>
        if (sp.start == 4)  openerB = true;   // 'b' in <b>
        if (sp.start == 8)  closerA = true;   // 'a' in </a>
        if (sp.start == 12) closerB = true;   // 'b' in </b>
    }
    QVERIFY(openerA);
    QVERIFY(openerB);
    QVERIFY(closerA);
    QVERIFY(closerB);
}

/// §4.3 — Captures at each stack level stay independent; diving into a
/// deeper context and coming back must not clobber the outer capture.
/// Direct unit of HighlightState + two capture lists at two depths.
void TestDynamicRules::captureDepth_innerContextDoesNotPollute() {
    HighlightState s;
    s.contextStack  = {0, 1};
    s.captureStack  = {{}, QStringList{QStringLiteral("outer"), QStringLiteral("o2")}};

    // Push a deeper level with its own captures.
    s.contextStack.append(2);
    s.captureStack.append(QStringList{QStringLiteral("inner")});

    QCOMPARE(s.captureStack.last().first(), QStringLiteral("inner"));
    QCOMPARE(s.captureStack.at(1).first(),  QStringLiteral("outer"));

    // Pop the deeper level — outer captures intact.
    s.contextStack.pop_back();
    s.captureStack.pop_back();
    QCOMPARE(s.captureStack.size(), 2);
    QCOMPARE(s.captureStack.last().first(), QStringLiteral("outer"));
}

/// §4.4 — States with matching contextStack but differing captureStack
/// compare UNEQUAL (incremental re-highlight must not treat them as
/// stable). Matching both stacks → equal, so the editor's cache still
/// stops after a no-op line.
void TestDynamicRules::stateEquality_capturesDistinguish() {
    HighlightState a, b;
    a.contextStack = {0, 1};
    a.captureStack = {{}, QStringList{QStringLiteral("foo")}};
    b.contextStack = {0, 1};
    b.captureStack = {{}, QStringList{QStringLiteral("bar")}};
    QVERIFY(a != b);

    b.captureStack = a.captureStack;
    QVERIFY(a == b);

    // Length mismatch in captures also counts as different.
    b.captureStack = {{}, QStringList{QStringLiteral("foo"), QStringLiteral("x")}};
    QVERIFY(a != b);
}

QTEST_GUILESS_MAIN(TestDynamicRules)
#include "test_dynamic_rules.moc"
