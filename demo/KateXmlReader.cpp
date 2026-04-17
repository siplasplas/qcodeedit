#include "KateXmlReader.h"

#include <QColor>
#include <QDebug>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVector>
#include <QXmlStreamReader>

using qce::HighlightContext;
using qce::HighlightRule;
using qce::KeywordList;
using qce::RulesHighlighter;
using qce::TextAttribute;

namespace {

// -----------------------------------------------------------------------------
// Built-in light theme: defStyleNum → colour (+ italic/bold hints).
// -----------------------------------------------------------------------------
struct ThemeEntry {
    QColor fg;
    bool bold = false;
    bool italic = false;
};

static const QHash<QString, ThemeEntry>& theme() {
    static const QHash<QString, ThemeEntry> kTheme = {
        {"dsNormal",        {QColor(0x00, 0x00, 0x00)}},
        {"dsKeyword",       {QColor(0x00, 0x00, 0xAA), /*bold*/true}},
        {"dsControlFlow",   {QColor(0x00, 0x00, 0xAA), /*bold*/true}},
        {"dsFunction",      {QColor(0x66, 0x44, 0x00)}},
        {"dsVariable",      {QColor(0x00, 0x40, 0x80)}},
        {"dsOperator",      {QColor(0x00, 0x00, 0x00)}},
        {"dsBuiltIn",       {QColor(0x60, 0x40, 0x80)}},
        {"dsExtension",     {QColor(0x00, 0x44, 0x44)}},
        {"dsPreprocessor",  {QColor(0x00, 0x66, 0x00)}},
        {"dsAttribute",     {QColor(0x00, 0x60, 0x80)}},
        {"dsChar",          {QColor(0x80, 0x40, 0x00)}},
        {"dsSpecialChar",   {QColor(0x30, 0x60, 0xA0)}},
        {"dsString",        {QColor(0xC0, 0x10, 0x10)}},
        {"dsVerbatimString",{QColor(0xC0, 0x10, 0x10)}},
        {"dsSpecialString", {QColor(0xA0, 0x30, 0x50)}},
        {"dsImport",        {QColor(0x00, 0x00, 0xAA)}},
        {"dsDataType",      {QColor(0x00, 0x80, 0x80), /*bold*/true}},
        {"dsDecVal",        {QColor(0x80, 0x40, 0x00)}},
        {"dsBaseN",         {QColor(0x80, 0x40, 0x00)}},
        {"dsFloat",         {QColor(0x80, 0x40, 0x00)}},
        {"dsConstant",      {QColor(0x00, 0x00, 0xAA)}},
        {"dsComment",       {QColor(0x80, 0x80, 0x80), false, /*italic*/true}},
        {"dsDocumentation", {QColor(0x60, 0x80, 0x80), false, /*italic*/true}},
        {"dsAnnotation",    {QColor(0x30, 0x60, 0x80)}},
        {"dsCommentVar",    {QColor(0x70, 0x50, 0x60)}},
        {"dsRegionMarker",  {QColor(0x00, 0x40, 0x80)}},
        {"dsInformation",   {QColor(0xB0, 0x80, 0x00)}},
        {"dsWarning",       {QColor(0xB0, 0x80, 0x00)}},
        {"dsAlert",         {QColor(0xFF, 0x00, 0x00), /*bold*/true}},
        {"dsError",         {QColor(0xFF, 0x00, 0x00), /*bold*/true}},
        {"dsOthers",        {QColor(0x00, 0x60, 0x00)}},
    };
    return kTheme;
}

// -----------------------------------------------------------------------------
// DTD entity pre-expansion
// -----------------------------------------------------------------------------
// Kate XML files use <!ENTITY name "..."> declarations, e.g. in c.xml:
//   <!ENTITY int "(?:[0-9](?:'?[0-9]++)*+)">
// ...and reference them as &int; inside regex bodies. Qt 6's XML reader does
// NOT replace general entities by default, so we scan the file ourselves,
// collect the definitions, and substitute &name; before handing the text to
// QXmlStreamReader.
static QString expandDtdEntities(const QString& raw) {
    // Find <!DOCTYPE ... [ ... ]>
    const int docStart = raw.indexOf(QStringLiteral("<!DOCTYPE"));
    if (docStart < 0) return raw;
    const int bracketOpen = raw.indexOf(QLatin1Char('['), docStart);
    const int bracketClose = raw.indexOf(QLatin1Char(']'), bracketOpen);
    if (bracketOpen < 0 || bracketClose < 0) return raw;

    const QString dtd = raw.mid(bracketOpen + 1, bracketClose - bracketOpen - 1);
    // Parse <!ENTITY name "value">
    static const QRegularExpression re(
        QStringLiteral("<!ENTITY\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+\"([^\"]*)\""));
    QMap<QString, QString> entities;
    auto it = re.globalMatch(dtd);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        entities.insert(m.captured(1), m.captured(2));
    }
    if (entities.isEmpty()) return raw;

    // Strip the DOCTYPE so Qt's parser doesn't complain about unresolved refs.
    const int docEnd = raw.indexOf(QLatin1Char('>'), bracketClose);
    QString out = raw;
    if (docEnd > docStart) {
        out.remove(docStart, docEnd - docStart + 1);
    }

    // Iterative expansion (entity bodies may reference other entities).
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        for (auto i = entities.begin(); i != entities.end(); ++i) {
            const QString ref = QLatin1Char('&') + i.key() + QLatin1Char(';');
            if (out.contains(ref)) {
                out.replace(ref, i.value());
                changed = true;
            }
        }
        if (!changed) break;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Raw (name-based) intermediate structures
// -----------------------------------------------------------------------------
struct RawRule {
    QString tag;
    QMap<QString, QString> attrs;
    int includedContextIdxHint = -1; // for IncludeRules fast path (not used here)
};
struct RawContext {
    QString name;
    QString attribute;         // itemData name
    QString lineEndContext;    // "#stay", "#pop", "Name", "#pop!Name" ...
    QVector<RawRule> rules;
};
struct RawList {
    QString name;
    QSet<QString> items;
};
struct RawItemData {
    QString name;
    QString defStyleNum;
    bool italic = false, bold = false, underline = false;
    bool italicSet = false, boldSet = false, underlineSet = false;
};

static bool attrBool(const QMap<QString, QString>& a, const QString& key,
                      bool defaultValue = false) {
    const auto it = a.find(key);
    if (it == a.end()) return defaultValue;
    const QString v = it.value().trimmed().toLower();
    return v == QLatin1String("1") || v == QLatin1String("true");
}

struct ContextSwitch {
    int popCount = 0;
    QString pushName; // empty if no push
};
static ContextSwitch parseContextSwitch(const QString& raw) {
    ContextSwitch r;
    if (raw.isEmpty() || raw == QLatin1String("#stay")) return r;
    QString rest = raw;
    while (rest.startsWith(QLatin1String("#pop"))) {
        ++r.popCount;
        rest.remove(0, 4);
    }
    if (rest.startsWith(QLatin1Char('!'))) rest.remove(0, 1);
    rest = rest.trimmed();
    if (!rest.isEmpty()) r.pushName = rest;
    return r;
}

} // namespace

// -----------------------------------------------------------------------------
// The reader
// -----------------------------------------------------------------------------
std::unique_ptr<RulesHighlighter> KateXmlReader::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "KateXmlReader: cannot open" << path;
        return nullptr;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    const QString src = expandDtdEntities(raw);
    QXmlStreamReader xml(src);

    QVector<RawContext>   rawContexts;
    QVector<RawList>      rawLists;
    QVector<RawItemData>  rawItemDatas;
    bool keywordsCaseSensitive = true;

    // -------- Pass 1: collect raw structures ---------------------------------
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const QString name = xml.name().toString();

        if (name == QLatin1String("list")) {
            RawList rl;
            rl.name = xml.attributes().value(QStringLiteral("name")).toString();
            while (!(xml.isEndElement() && xml.name() == QLatin1String("list"))
                    && !xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("item")) {
                    rl.items.insert(xml.readElementText().trimmed());
                }
            }
            rawLists.push_back(std::move(rl));
        }
        else if (name == QLatin1String("context")) {
            RawContext rc;
            const auto attrs = xml.attributes();
            rc.name           = attrs.value(QStringLiteral("name")).toString();
            rc.attribute      = attrs.value(QStringLiteral("attribute")).toString();
            rc.lineEndContext = attrs.value(QStringLiteral("lineEndContext")).toString();
            while (!(xml.isEndElement() && xml.name() == QLatin1String("context"))
                    && !xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    RawRule rr;
                    rr.tag = xml.name().toString();
                    const auto ra = xml.attributes();
                    for (const QXmlStreamAttribute& a : ra) {
                        rr.attrs.insert(a.name().toString(), a.value().toString());
                    }
                    rc.rules.push_back(std::move(rr));
                }
            }
            rawContexts.push_back(std::move(rc));
        }
        else if (name == QLatin1String("itemData")) {
            RawItemData rid;
            const auto a = xml.attributes();
            rid.name        = a.value(QStringLiteral("name")).toString();
            rid.defStyleNum = a.value(QStringLiteral("defStyleNum")).toString();
            if (a.hasAttribute(QStringLiteral("italic"))) {
                rid.italic = attrBool(QMap<QString, QString>{
                    {QStringLiteral("italic"),
                     a.value(QStringLiteral("italic")).toString()}},
                    QStringLiteral("italic"));
                rid.italicSet = true;
            }
            if (a.hasAttribute(QStringLiteral("bold"))) {
                rid.bold = attrBool(QMap<QString, QString>{
                    {QStringLiteral("bold"),
                     a.value(QStringLiteral("bold")).toString()}},
                    QStringLiteral("bold"));
                rid.boldSet = true;
            }
            if (a.hasAttribute(QStringLiteral("underline"))) {
                rid.underline = attrBool(QMap<QString, QString>{
                    {QStringLiteral("underline"),
                     a.value(QStringLiteral("underline")).toString()}},
                    QStringLiteral("underline"));
                rid.underlineSet = true;
            }
            rawItemDatas.push_back(std::move(rid));
        }
        else if (name == QLatin1String("keywords")) {
            const auto a = xml.attributes();
            if (a.hasAttribute(QStringLiteral("casesensitive"))) {
                keywordsCaseSensitive = attrBool(QMap<QString, QString>{
                    {QStringLiteral("casesensitive"),
                     a.value(QStringLiteral("casesensitive")).toString()}},
                    QStringLiteral("casesensitive"), true);
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "KateXmlReader:" << path << ":" << xml.errorString();
        return nullptr;
    }
    if (rawContexts.isEmpty()) {
        qWarning() << "KateXmlReader:" << path << "— no contexts found";
        return nullptr;
    }

    // -------- Pass 2: build the highlighter ---------------------------------
    auto hl = std::make_unique<RulesHighlighter>();
    const auto& th = theme();

    QHash<QString, int> attrIdByName;
    for (const RawItemData& rid : rawItemDatas) {
        TextAttribute ta;
        const auto tIt = th.find(rid.defStyleNum);
        if (tIt != th.end()) {
            ta.foreground = tIt->fg;
            ta.bold       = tIt->bold;
            ta.italic     = tIt->italic;
        }
        if (rid.italicSet)    ta.italic    = rid.italic;
        if (rid.boldSet)      ta.bold      = rid.bold;
        if (rid.underlineSet) ta.underline = rid.underline;
        attrIdByName.insert(rid.name, hl->addAttribute(ta));
    }

    QHash<QString, int> klIdByName;
    for (const RawList& rl : rawLists) {
        klIdByName.insert(rl.name, hl->addKeywordList({rl.name, rl.items,
                                                       keywordsCaseSensitive}));
    }

    // First pass on contexts: create empty contexts so that forward-references
    // in rule attributes resolve to valid ids. We fill rules in the second
    // loop below.
    QHash<QString, int> ctxIdByName;
    for (const RawContext& rc : rawContexts) {
        HighlightContext hc;
        hc.name = rc.name;
        const int id = hl->addContext(std::move(hc));
        ctxIdByName.insert(rc.name, id);
    }

    auto resolveAttr = [&](const QString& name) -> int {
        return name.isEmpty() ? -1 : attrIdByName.value(name, -1);
    };
    auto resolveCtx = [&](const QString& name) -> int {
        return name.isEmpty() ? -1 : ctxIdByName.value(name, -1);
    };

    for (const RawContext& rc : rawContexts) {
        HighlightContext& hc = hl->contextRef(ctxIdByName.value(rc.name));
        hc.defaultAttribute = resolveAttr(rc.attribute);
        const ContextSwitch leSw = parseContextSwitch(rc.lineEndContext);
        hc.lineEndPopCount    = leSw.popCount;
        hc.lineEndNextContext = resolveCtx(leSw.pushName);

        for (const RawRule& rr : rc.rules) {
            HighlightRule hr;
            const int attr = resolveAttr(rr.attrs.value(QStringLiteral("attribute")));
            const ContextSwitch sw = parseContextSwitch(
                rr.attrs.value(QStringLiteral("context")));
            hr.attributeId    = attr;
            hr.popCount       = sw.popCount;
            hr.nextContextId  = resolveCtx(sw.pushName);
            hr.lookAhead      = attrBool(rr.attrs, QStringLiteral("lookAhead"));
            hr.firstNonSpace  = attrBool(rr.attrs, QStringLiteral("firstNonSpace"));

            const bool insensitive = attrBool(rr.attrs, QStringLiteral("insensitive"));
            hr.caseSensitive = !insensitive;

            auto charAttr = [&](const QString& key) -> QChar {
                const QString v = rr.attrs.value(key);
                return v.isEmpty() ? QChar() : v.at(0);
            };

            if (rr.tag == QLatin1String("DetectChar")) {
                hr.kind = HighlightRule::DetectChar;
                hr.ch = charAttr(QStringLiteral("char"));
            } else if (rr.tag == QLatin1String("Detect2Chars")) {
                hr.kind = HighlightRule::Detect2Chars;
                hr.ch  = charAttr(QStringLiteral("char"));
                hr.ch1 = charAttr(QStringLiteral("char1"));
            } else if (rr.tag == QLatin1String("AnyChar")) {
                hr.kind = HighlightRule::AnyChar;
                hr.str  = rr.attrs.value(QStringLiteral("String"));
            } else if (rr.tag == QLatin1String("StringDetect")) {
                hr.kind = HighlightRule::StringDetect;
                hr.str  = rr.attrs.value(QStringLiteral("String"));
            } else if (rr.tag == QLatin1String("WordDetect")) {
                hr.kind = HighlightRule::WordDetect;
                hr.str  = rr.attrs.value(QStringLiteral("String"));
            } else if (rr.tag == QLatin1String("RegExpr")) {
                hr.kind = HighlightRule::RegExpr;
                QRegularExpression::PatternOptions opts =
                    QRegularExpression::UseUnicodePropertiesOption;
                if (insensitive) opts |= QRegularExpression::CaseInsensitiveOption;
                hr.regex = QRegularExpression(
                    rr.attrs.value(QStringLiteral("String")), opts);
            } else if (rr.tag == QLatin1String("keyword")) {
                hr.kind = HighlightRule::Keyword;
                hr.keywordListId = klIdByName.value(
                    rr.attrs.value(QStringLiteral("String")), -1);
            } else if (rr.tag == QLatin1String("DetectSpaces")) {
                hr.kind = HighlightRule::DetectSpaces;
            } else if (rr.tag == QLatin1String("DetectIdentifier")) {
                hr.kind = HighlightRule::DetectIdentifier;
            } else if (rr.tag == QLatin1String("Int")) {
                hr.kind = HighlightRule::Int;
            } else if (rr.tag == QLatin1String("Float")) {
                hr.kind = HighlightRule::Float;
            } else if (rr.tag == QLatin1String("HlCStringChar")) {
                hr.kind = HighlightRule::HlCStringChar;
            } else if (rr.tag == QLatin1String("LineContinue")) {
                hr.kind = HighlightRule::LineContinue;
            } else if (rr.tag == QLatin1String("RangeDetect")) {
                hr.kind = HighlightRule::RangeDetect;
                hr.ch  = charAttr(QStringLiteral("char"));
                hr.ch1 = charAttr(QStringLiteral("char1"));
            } else if (rr.tag == QLatin1String("IncludeRules")) {
                const QString ctxRef = rr.attrs.value(QStringLiteral("context"));
                if (ctxRef.startsWith(QStringLiteral("##"))) {
                    // Cross-file include — ignored in v1.
                    continue;
                }
                hr.kind = HighlightRule::IncludeRules;
                hr.includedContextId = resolveCtx(ctxRef);
                if (hr.includedContextId < 0) continue; // unresolved
            } else {
                // Unknown rule tag — skip.
                continue;
            }
            hc.rules.push_back(std::move(hr));
        }
    }

    hl->setInitialContextId(ctxIdByName.value(rawContexts.first().name, 0));
    return hl;
}
