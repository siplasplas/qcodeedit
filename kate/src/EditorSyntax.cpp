#include <qce/kate/EditorSyntax.h>

#include <qce/kate/KatePaths.h>
#include <qce/kate/KateXmlReader.h>

#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/GutterColors.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>

#include <QFileInfo>
#include <QStringList>

namespace qce::kate {
namespace {

KateSyntaxIndex& indexStorage() {
    static KateSyntaxIndex index = [] {
        KateSyntaxIndex loaded = KateSyntaxIndex::load(dataDir());
        loaded.saveIfDirty();
        return loaded;
    }();
    return index;
}

// Used only when no Kate definition matches (e.g. data not downloaded yet).
bool isCppExtension(const QString& extension) {
    static const QStringList cpp = {"c", "cpp", "cxx", "cc", "h", "hpp", "hxx"};
    return cpp.contains(extension.toLower());
}

} // namespace

const KateSyntaxIndex& syntaxIndex() {
    return indexStorage();
}

void reloadSyntaxIndex() {
    KateSyntaxIndex& index = indexStorage();
    index = KateSyntaxIndex::load(dataDir());
    index.saveIfDirty();
}

EditorSyntax::EditorSyntax(CodeEdit* edit) : m_edit(edit), m_defaultPalette(edit->area()->palette()) {
    m_theme = KateTheme::loadDefault();
    applyThemePalette();
}

// The area must not keep pointers to the highlighter and provider that go now.
EditorSyntax::~EditorSyntax() {
    if (m_highlighter) clear();
}

void EditorSyntax::setAutomatic() {
    m_mode = Mode::Automatic;
    m_syntaxFile.clear();
}

void EditorSyntax::setPlainText() {
    m_mode = Mode::PlainText;
    m_syntaxFile.clear();
}

void EditorSyntax::setManual(const QString& syntaxFile) {
    m_mode = Mode::Manual;
    m_syntaxFile = syntaxFile;
}

void EditorSyntax::apply() {
    if (!m_themeFile.isEmpty()) {
        const KateTheme theme = KateTheme::load(m_themeFile);
        if (theme.isValid()) m_theme = theme;
    } else {
        // "Default": Breeze Light/Dark to match the desktop, or the editor's
        // built-in black on white when the themes are not downloaded.
        m_theme = KateTheme::loadDefault();
    }
    applyThemePalette();
    if (m_mode == Mode::PlainText) {
        clear();
    } else if (m_mode == Mode::Manual) {
        const KateSyntaxIndex& index = syntaxIndex();
        const LanguageEntry* entry = index.byFile(m_syntaxFile);
        if (!entry || entry->unsupported || !applyKate(index.filePath(*entry))) clear();
    } else {
        applyForFile(m_fileName);
    }
}

void EditorSyntax::applyThemePalette() {
    QPalette palette = m_defaultPalette;
    if (m_theme.editorBackground.isValid()) {
        palette.setColor(QPalette::Base, m_theme.editorBackground);
        palette.setColor(QPalette::Window, m_theme.editorBackground);
    }
    const auto normal = m_theme.styles.constFind(QStringLiteral("Normal"));
    if (normal != m_theme.styles.constEnd() && normal->fg.isValid()) {
        palette.setColor(QPalette::Text, normal->fg);
        palette.setColor(QPalette::WindowText, normal->fg);
    }
    m_edit->setPalette(palette);
    m_edit->area()->setPalette(palette);
    m_edit->area()->viewport()->setPalette(palette);
    // The line with the caret: the background a little darker (white gives
    // #F8F8F8), or lighter on a dark theme; the theme's tint stays.
    const QColor base = palette.color(QPalette::Base);
    m_edit->area()->setCurrentLineColor(base.lightness() > 128 ? base.darker(103) : base.lighter(140));
    m_edit->area()->viewport()->update();
    // Invalid colours (no theme, or missing in it) are derived from the palette.
    m_edit->setGutterColors({m_theme.iconBorder, m_theme.lineNumbers, m_theme.separator});
}

TextAttribute EditorSyntax::themedAttribute(const QString& style, const TextAttribute& fallback) const {
    const auto entry = m_theme.styles.constFind(style);
    if (entry == m_theme.styles.constEnd()) return fallback;
    return {entry->fg.isValid() ? entry->fg : fallback.foreground, entry->bg, entry->bold, entry->italic,
            entry->underline};
}

void EditorSyntax::applyForFile(const QString& path) {
    // Kate definitions downloaded by qcodeedit, matched by file name and
    // sorted by priority (definitions needing a newer Kate are skipped).
    const KateSyntaxIndex& index = syntaxIndex();
    const auto matches = index.forFileName(path);
    if (!matches.isEmpty()) {
        if (!applyKate(index.filePath(*matches.first()))) clear();
        return;
    }
    // No Kate definition: the built-in highlighter for C and C++.
    if (isCppExtension(QFileInfo(path).suffix())) {
        applyCpp();
        return;
    }
    clear();
}

bool EditorSyntax::applyKate(const QString& xmlPath) {
    // ##Lang includes (e.g. Doxygen inside C++) are resolved through the index.
    auto highlighter = KateXmlReader::load(xmlPath, m_theme, syntaxIndex());
    if (!highlighter) return false;
    install(std::move(highlighter), {{"Brace1", "{…}"},
                                     {"brace", "{…}"},
                                     {"curly", "{…}"},
                                     {"square", "[…]"},
                                     {"paren", "(…)"},
                                     {"Comment", "/*…*/"},
                                     {"Region1", "//BEGIN…END"}});
    return true;
}

void EditorSyntax::install(std::unique_ptr<RulesHighlighter> highlighter,
                           const QList<QPair<QString, QString>>& placeholders) {
    // Detach the old provider before its highlighter goes away.
    m_edit->area()->setFoldingProvider(nullptr);
    m_highlighter = std::move(highlighter);
    m_edit->area()->setHighlighter(m_highlighter.get());
    m_foldProvider = std::make_unique<RuleBasedFoldingProvider>(m_highlighter.get());
    for (const auto& [group, text] : placeholders) m_foldProvider->setPlaceholderFor(group, text);
    m_edit->area()->setFoldingProvider(m_foldProvider.get());
}

void EditorSyntax::clear() {
    m_edit->area()->setHighlighter(nullptr);
    m_edit->area()->setFoldingProvider(nullptr);
    m_highlighter.reset();
    m_foldProvider.reset();
}

void EditorSyntax::applyCpp() {
    auto hl = std::make_unique<RulesHighlighter>();

    const int attrKw = hl->addAttribute(themedAttribute(QStringLiteral("Keyword"), {QColor(0x00, 0x00, 0xAA), {}, true}));
    const int attrType = hl->addAttribute(themedAttribute(QStringLiteral("DataType"), {QColor(0x00, 0x80, 0x80), {}, true}));
    const int attrString = hl->addAttribute(themedAttribute(QStringLiteral("String"), {QColor(0xC0, 0x10, 0x10)}));
    const int attrComment = hl->addAttribute(themedAttribute(QStringLiteral("Comment"), {QColor(0x80, 0x80, 0x80), {}, false, true}));
    const int attrNumber = hl->addAttribute(themedAttribute(QStringLiteral("DecVal"), {QColor(0x80, 0x40, 0x00)}));
    const int attrPP = hl->addAttribute(themedAttribute(QStringLiteral("Preprocessor"), {QColor(0x60, 0x00, 0x80)}));

    const int klKeywords = hl->addKeywordList({"keywords", {
        "if","else","return","for","while","do","break","continue",
        "switch","case","default","goto","sizeof","alignof","noexcept",
        "try","catch","throw","new","delete","operator","template",
        "typename","namespace","using","class","struct","enum","union",
        "public","protected","private","virtual","override","final",
        "static","inline","explicit","friend","mutable","volatile",
        "extern","register","typedef","nullptr","true","false","this",
    }, true});

    const int klTypes = hl->addKeywordList({"types", {
        "int","void","char","bool","float","double","long","short",
        "unsigned","signed","size_t","auto","const","constexpr",
        "int8_t","int16_t","int32_t","int64_t",
        "uint8_t","uint16_t","uint32_t","uint64_t",
        "wchar_t","char8_t","char16_t","char32_t",
    }, true});

    // Contexts
    HighlightContext ctxN{"Normal",       -1,          -1, 0, false, -1, {}};
    HighlightContext ctxS{"String",       attrString,  -1, 0, false, -1, {}};
    HighlightContext ctxC{"Char",         attrString,  -1, 0, false, -1, {}};
    HighlightContext ctxB{"BlockComment", attrComment, -1, 0, false, -1, {}};
    HighlightContext ctxL{"LineComment",  attrComment,  0, 1, false, -1, {}};
    HighlightContext ctxP{"Preprocessor", attrPP,      0, 1, false, -1, {}};

    const int normal       = hl->addContext(ctxN);
    const int strCtx       = hl->addContext(ctxS);
    const int charCtx      = hl->addContext(ctxC);
    const int blockComment = hl->addContext(ctxB);
    const int lineComment  = hl->addContext(ctxL);
    const int preprocessor = hl->addContext(ctxP);

    // Folding region IDs
    const int rgCurly = hl->regionIdForName("curly");
    const int rgComm  = hl->regionIdForName("Comment");

    auto& norm = hl->contextRef(normal);

    // Preprocessor lines (#include, #define, …)
    norm.rules.push_back({HighlightRule::DetectChar, '#', {}, {}, true, {}, -1, -1,
                          attrPP, preprocessor, 0, true/*firstNonSpace*/, false});
    // Line comment //
    norm.rules.push_back({HighlightRule::Detect2Chars, '/', '/', {}, true, {}, -1, -1,
                          attrComment, lineComment, 0, false, false});
    // Block comment /* — also starts Comment fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::Detect2Chars;
        r.ch = '/'; r.ch1 = '*';
        r.attributeId = attrComment; r.nextContextId = blockComment;
        r.beginRegionId = rgComm;
        norm.rules.push_back(r);
    }
    // String "
    norm.rules.push_back({HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
                          attrString, strCtx, 0, false, false});
    // Char '
    norm.rules.push_back({HighlightRule::DetectChar, '\'', {}, {}, true, {}, -1, -1,
                          attrString, charCtx, 0, false, false});
    // Types (before keywords — both match identifiers)
    norm.rules.push_back({HighlightRule::Keyword, {}, {}, {}, true, {}, klTypes, -1,
                          attrType, -1, 0, false, false});
    // Keywords
    norm.rules.push_back({HighlightRule::Keyword, {}, {}, {}, true, {}, klKeywords, -1,
                          attrKw, -1, 0, false, false});
    // Numbers
    { HighlightRule r; r.kind = HighlightRule::Int;   r.attributeId = attrNumber; norm.rules.push_back(r); }
    { HighlightRule r; r.kind = HighlightRule::Float; r.attributeId = attrNumber; norm.rules.push_back(r); }
    // Opening brace { — starts curly fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::DetectChar; r.ch = '{';
        r.beginRegionId = rgCurly;
        norm.rules.push_back(r);
    }
    // Closing brace } — ends curly fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::DetectChar; r.ch = '}';
        r.endRegionId = rgCurly;
        norm.rules.push_back(r);
    }

    // String context
    auto& str = hl->contextRef(strCtx);
    str.rules.push_back({HighlightRule::HlCStringChar, {}, {}, {}, true, {}, -1, -1,
                         -1, -1, 0, false, false});
    str.rules.push_back({HighlightRule::DetectChar, '"', {}, {}, true, {}, -1, -1,
                         attrString, -1, 1, false, false});

    // Char context
    auto& ch = hl->contextRef(charCtx);
    ch.rules.push_back({HighlightRule::HlCStringChar, {}, {}, {}, true, {}, -1, -1,
                        -1, -1, 0, false, false});
    ch.rules.push_back({HighlightRule::DetectChar, '\'', {}, {}, true, {}, -1, -1,
                        attrString, -1, 1, false, false});

    // Block comment context — closing */ ends Comment fold region
    {
        HighlightRule r;
        r.kind = HighlightRule::Detect2Chars; r.ch = '*'; r.ch1 = '/';
        r.attributeId = attrComment; r.nextContextId = -1; r.popCount = 1;
        r.endRegionId = rgComm;
        hl->contextRef(blockComment).rules.push_back(r);
    }

    hl->setInitialContextId(normal);
    install(std::move(hl), {{"curly", "{…}"}, {"Comment", "/*…*/"}});
}

} // namespace qce::kate
