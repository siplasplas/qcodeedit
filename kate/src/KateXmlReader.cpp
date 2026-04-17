#include <qce/kate/KateXmlReader.h>

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

// Find the closing ']' of a DTD internal subset, skipping quoted strings so
// that ']' characters inside entity values (e.g. "[]{|}") are not mistaken
// for the closing bracket.
static int findDtdBracketClose(const QString& raw, int from) {
    bool inQuote = false;
    QChar quoteChar;
    for (int i = from; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (inQuote) {
            if (c == quoteChar) inQuote = false;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inQuote = true;
            quoteChar = c;
        } else if (c == QLatin1Char(']')) {
            return i;
        }
    }
    return -1;
}

static QString expandDtdEntities(const QString& raw) {
    const int docStart = raw.indexOf(QStringLiteral("<!DOCTYPE"));
    if (docStart < 0) return raw;
    const int bracketOpen = raw.indexOf(QLatin1Char('['), docStart);
    const int bracketClose = findDtdBracketClose(raw, bracketOpen + 1);
    if (bracketOpen < 0 || bracketClose < 0) return raw;

    const QString dtd = raw.mid(bracketOpen + 1, bracketClose - bracketOpen - 1);
    static const QRegularExpression re(
        QStringLiteral("<!ENTITY\\s+([A-Za-z_][A-Za-z0-9_]*)\\s+\"([^\"]*)\""));
    QMap<QString, QString> entities;
    auto it = re.globalMatch(dtd);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        entities.insert(m.captured(1), m.captured(2));
    }
    if (entities.isEmpty()) return raw;

    const int docEnd = raw.indexOf(QLatin1Char('>'), bracketClose);
    QString out = raw;
    if (docEnd > docStart)
        out.remove(docStart, docEnd - docStart + 1);

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
// Raw intermediate structures
// -----------------------------------------------------------------------------
struct RawRule {
    QString tag;
    QMap<QString, QString> attrs;
};
struct RawContext {
    QString name;
    QString attribute;
    QString lineEndContext;
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
struct LangData {
    QString              name;
    QVector<RawContext>  contexts;
    QVector<RawList>     lists;
    QVector<RawItemData> itemDatas;
    bool                 caseSensitive = true;
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
    QString pushName;
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

// -----------------------------------------------------------------------------
// Parse a single Kate XML file into raw structures
// -----------------------------------------------------------------------------
static LangData parseKateFile(const QString& path) {
    LangData data;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "KateXmlReader: cannot open" << path;
        return data;
    }
    const QString raw = QString::fromUtf8(f.readAll());
    const QString src = expandDtdEntities(raw);
    QXmlStreamReader xml(src);

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        const QString name = xml.name().toString();

        if (name == QLatin1String("language")) {
            data.name = xml.attributes().value(QStringLiteral("name")).toString();
        } else if (name == QLatin1String("list")) {
            RawList rl;
            rl.name = xml.attributes().value(QStringLiteral("name")).toString();
            while (!(xml.isEndElement() && xml.name() == QLatin1String("list"))
                   && !xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("item"))
                    rl.items.insert(xml.readElementText().trimmed());
            }
            data.lists.push_back(std::move(rl));
        } else if (name == QLatin1String("context")) {
            RawContext rc;
            const auto a = xml.attributes();
            rc.name           = a.value(QStringLiteral("name")).toString();
            rc.attribute      = a.value(QStringLiteral("attribute")).toString();
            rc.lineEndContext = a.value(QStringLiteral("lineEndContext")).toString();
            while (!(xml.isEndElement() && xml.name() == QLatin1String("context"))
                   && !xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement()) {
                    RawRule rr;
                    rr.tag = xml.name().toString();
                    for (const QXmlStreamAttribute& xa : xml.attributes())
                        rr.attrs.insert(xa.name().toString(), xa.value().toString());
                    rc.rules.push_back(std::move(rr));
                }
            }
            data.contexts.push_back(std::move(rc));
        } else if (name == QLatin1String("itemData")) {
            RawItemData rid;
            const auto a = xml.attributes();
            rid.name        = a.value(QStringLiteral("name")).toString();
            rid.defStyleNum = a.value(QStringLiteral("defStyleNum")).toString();
            auto readBool = [&](const QString& key, bool& dst, bool& set) {
                if (!a.hasAttribute(key)) return;
                const QString v = a.value(key).toString().trimmed().toLower();
                dst = (v == QLatin1String("1") || v == QLatin1String("true"));
                set = true;
            };
            readBool(QStringLiteral("italic"),    rid.italic,    rid.italicSet);
            readBool(QStringLiteral("bold"),      rid.bold,      rid.boldSet);
            readBool(QStringLiteral("underline"), rid.underline, rid.underlineSet);
            data.itemDatas.push_back(std::move(rid));
        } else if (name == QLatin1String("keywords")) {
            const auto a = xml.attributes();
            if (a.hasAttribute(QStringLiteral("casesensitive"))) {
                const QString v = a.value(QStringLiteral("casesensitive"))
                                   .toString().trimmed().toLower();
                data.caseSensitive = (v == QLatin1String("1") || v == QLatin1String("true"));
            }
        }
    }

    if (xml.hasError())
        qWarning() << "KateXmlReader:" << path << ":" << xml.errorString();

    return data;
}

// -----------------------------------------------------------------------------
// Directory index: language name → file path
// -----------------------------------------------------------------------------
static QHash<QString, QString> buildSyntaxIndex(const QString& dir) {
    static const QRegularExpression nameRe(
        QStringLiteral("<language[^>]+name=\"([^\"]+)\""));
    QHash<QString, QString> index;
    const QStringList entries = QDir(dir).entryList({"*.xml"}, QDir::Files);
    for (const QString& entry : entries) {
        const QString filePath = dir + QLatin1Char('/') + entry;
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString head = QString::fromUtf8(f.read(2048));
        f.close();
        const auto m = nameRe.match(head);
        if (m.hasMatch())
            index.insert(m.captured(1), filePath);
    }
    return index;
}

// -----------------------------------------------------------------------------
// Resolved IDs within a shared RulesHighlighter
// -----------------------------------------------------------------------------
struct ResolvedLang {
    QHash<QString, int> attrByName;
    QHash<QString, int> klByName;
    QHash<QString, int> ctxByName;
    QString             initialCtxName;
};

// -----------------------------------------------------------------------------
// Loader: manages loading multiple Kate language files into one highlighter
// -----------------------------------------------------------------------------
class Loader {
public:
    Loader(const QString& syntaxDir, RulesHighlighter* hl)
        : m_dir(syntaxDir), m_hl(hl) {}

    // Load the main (top-level) language file and set the initial context.
    // Returns false on failure.
    bool loadMain(const QString& path) {
        LangData data = parseKateFile(path);
        if (data.contexts.isEmpty()) {
            qWarning() << "KateXmlReader:" << path << "— no contexts found";
            return false;
        }
        // Use empty string as the key for the main language so same-language
        // context references resolve without any prefix.
        doLoad(QString(), data);
        const auto it = m_resolved.find(QString());
        if (it == m_resolved.end()) return false;
        m_hl->setInitialContextId(it->ctxByName.value(it->initialCtxName, 0));
        return true;
    }

private:
    QString          m_dir;
    RulesHighlighter* m_hl;

    QHash<QString, QString>      m_index;       // langName → file path (lazy)
    bool                         m_indexBuilt = false;
    QHash<QString, ResolvedLang> m_resolved;    // langName → resolved IDs
    QSet<QString>                m_inProgress;  // cycle detection

    void ensureIndex() {
        if (m_indexBuilt) return;
        m_index = buildSyntaxIndex(m_dir);
        m_indexBuilt = true;
    }

    // Ensure language `langName` is loaded. Returns pointer or nullptr.
    const ResolvedLang* ensureLoaded(const QString& langName) {
        if (m_resolved.contains(langName)) return &m_resolved[langName];
        if (m_inProgress.contains(langName)) {
            qWarning() << "KateXmlReader: circular IncludeRules for language" << langName;
            return nullptr;
        }
        ensureIndex();
        const QString path = m_index.value(langName);
        if (path.isEmpty()) {
            qWarning() << "KateXmlReader: language not found in syntax directory:" << langName;
            return nullptr;
        }
        LangData data = parseKateFile(path);
        if (data.contexts.isEmpty()) return nullptr;

        m_inProgress.insert(langName);
        doLoad(langName, data);
        m_inProgress.remove(langName);

        return m_resolved.contains(langName) ? &m_resolved[langName] : nullptr;
    }

    // Build a LangData into the shared highlighter and store the result.
    void doLoad(const QString& langName, const LangData& data) {
        ResolvedLang resolved;
        resolved.initialCtxName = data.contexts.isEmpty()
                                  ? QString()
                                  : data.contexts.first().name;
        const auto& th = theme();

        // Step 1: attributes
        for (const RawItemData& rid : data.itemDatas) {
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
            resolved.attrByName.insert(rid.name, m_hl->addAttribute(ta));
        }

        // Step 2: keyword lists
        for (const RawList& rl : data.lists) {
            resolved.klByName.insert(rl.name,
                m_hl->addKeywordList({rl.name, rl.items, data.caseSensitive}));
        }

        // Step 3: register context stubs (so forward refs & cross-lang cycles work)
        for (const RawContext& rc : data.contexts) {
            HighlightContext hc;
            hc.name = rc.name;
            resolved.ctxByName.insert(rc.name, m_hl->addContext(std::move(hc)));
        }

        // Store before filling rules so recursive ensureLoaded can find us.
        m_resolved.insert(langName, resolved);

        auto resolveAttr = [&](const QString& n) -> int {
            return n.isEmpty() ? -1 : resolved.attrByName.value(n, -1);
        };
        auto resolveCtxLocal = [&](const QString& n) -> int {
            return n.isEmpty() ? -1 : resolved.ctxByName.value(n, -1);
        };

        // Step 4: fill context rules.
        // IMPORTANT: ensureLoaded() called for cross-language ## references may
        // call m_hl->addContext(), reallocating the internal QVector and
        // invalidating any previously-taken HighlightContext reference.
        // We therefore collect rules into a local vector and write them back
        // via a fresh contextRef() call after the inner loop completes.
        struct PendingCtx {
            int                     ctxId;
            int                     defaultAttribute;
            int                     lineEndPopCount;
            int                     lineEndNextContext;
            QVector<HighlightRule>  rules;
        };
        QVector<PendingCtx> pending;
        pending.reserve(data.contexts.size());

        for (const RawContext& rc : data.contexts) {
            const int ctxId = resolved.ctxByName.value(rc.name);
            const ContextSwitch leSw = parseContextSwitch(rc.lineEndContext);
            PendingCtx pc;
            pc.ctxId             = ctxId;
            pc.defaultAttribute  = resolveAttr(rc.attribute);
            pc.lineEndPopCount   = leSw.popCount;
            pc.lineEndNextContext = resolveCtxLocal(leSw.pushName);

            for (const RawRule& rr : rc.rules) {
                HighlightRule hr;
                const int attr = resolveAttr(rr.attrs.value(QStringLiteral("attribute")));
                const ContextSwitch sw =
                    parseContextSwitch(rr.attrs.value(QStringLiteral("context")));
                hr.attributeId   = attr;
                hr.popCount      = sw.popCount;
                hr.nextContextId = resolveCtxLocal(sw.pushName);
                hr.lookAhead     = attrBool(rr.attrs, QStringLiteral("lookAhead"));
                hr.firstNonSpace = attrBool(rr.attrs, QStringLiteral("firstNonSpace"));

                const QString beginR = rr.attrs.value(QStringLiteral("beginRegion"));
                const QString endR   = rr.attrs.value(QStringLiteral("endRegion"));
                if (!beginR.isEmpty()) hr.beginRegionId = m_hl->regionIdForName(beginR);
                if (!endR.isEmpty())   hr.endRegionId   = m_hl->regionIdForName(endR);

                const bool insensitive = attrBool(rr.attrs, QStringLiteral("insensitive"));
                hr.caseSensitive = !insensitive;

                auto charAttr = [&](const QString& key) -> QChar {
                    const QString v = rr.attrs.value(key);
                    return v.isEmpty() ? QChar() : v.at(0);
                };

                if (rr.tag == QLatin1String("IncludeRules")) {
                    const QString ctxRef = rr.attrs.value(QStringLiteral("context"));
                    const int sep = ctxRef.indexOf(QStringLiteral("##"));
                    if (sep >= 0) {
                        // Cross-language: [CtxName]##LangName
                        // ensureLoaded may call addContext → realloc, but we
                        // accumulate into pc.rules (local), not into hc.rules.
                        const QString foreignLang = ctxRef.mid(sep + 2);
                        const QString foreignCtxName =
                            sep > 0 ? ctxRef.left(sep) : QString();
                        const ResolvedLang* foreign = ensureLoaded(foreignLang);
                        if (!foreign) continue;
                        const QString key = foreignCtxName.isEmpty()
                                            ? foreign->initialCtxName
                                            : foreignCtxName;
                        const int fCtxId = foreign->ctxByName.value(key, -1);
                        if (fCtxId < 0) {
                            qWarning() << "KateXmlReader: context not found:"
                                       << ctxRef;
                            continue;
                        }
                        hr.kind = HighlightRule::IncludeRules;
                        hr.includedContextId = fCtxId;
                    } else {
                        // Same-language include
                        hr.kind = HighlightRule::IncludeRules;
                        hr.includedContextId = resolveCtxLocal(ctxRef);
                        if (hr.includedContextId < 0) continue;
                    }
                } else if (rr.tag == QLatin1String("DetectChar")) {
                    hr.kind = HighlightRule::DetectChar;
                    hr.ch   = charAttr(QStringLiteral("char"));
                } else if (rr.tag == QLatin1String("Detect2Chars")) {
                    hr.kind = HighlightRule::Detect2Chars;
                    hr.ch   = charAttr(QStringLiteral("char"));
                    hr.ch1  = charAttr(QStringLiteral("char1"));
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
                    if (insensitive)
                        opts |= QRegularExpression::CaseInsensitiveOption;
                    hr.regex = QRegularExpression(
                        rr.attrs.value(QStringLiteral("String")), opts);
                } else if (rr.tag == QLatin1String("keyword")) {
                    hr.kind = HighlightRule::Keyword;
                    hr.keywordListId = resolved.klByName.value(
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
                    hr.ch   = charAttr(QStringLiteral("char"));
                    hr.ch1  = charAttr(QStringLiteral("char1"));
                } else {
                    continue; // unknown rule tag
                }

                pc.rules.push_back(std::move(hr));
            }
            pending.push_back(std::move(pc));
        }

        // Write back: all ensureLoaded calls are done, no more reallocs expected.
        for (PendingCtx& pc : pending) {
            HighlightContext& hc = m_hl->contextRef(pc.ctxId);
            hc.defaultAttribute   = pc.defaultAttribute;
            hc.lineEndPopCount    = pc.lineEndPopCount;
            hc.lineEndNextContext = pc.lineEndNextContext;
            hc.rules              = std::move(pc.rules);
        }
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Public entry point
// -----------------------------------------------------------------------------
std::unique_ptr<RulesHighlighter> KateXmlReader::load(const QString& path) {
    const QString dir = QFileInfo(path).absolutePath();
    auto hl = std::make_unique<RulesHighlighter>();
    Loader loader(dir, hl.get());
    if (!loader.loadMain(path)) return nullptr;
    return hl;
}
