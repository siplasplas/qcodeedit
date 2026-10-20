#include <qce/spell/GrammarChecker.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <map>
#include <mutex>

#ifdef QCE_SPELL_GRAMMAR
#include <qce/grammar/Grammar.h>
#include <qce/grammar/Morphology.h>
#endif

namespace qce::spell {

QString grammarDataDir() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(data).filePath(QStringLiteral("siplasplas/spell/grammar"));
}

#ifdef QCE_SPELL_GRAMMAR

namespace {

// A message for the user of a rule's finding.
QString messageFor(const std::string& rule) {
    if (rule == "government")
        return QCoreApplication::translate("qce::spell::GrammarChecker", "This word does not fit after the preposition");
    if (rule == "agreement")
        return QCoreApplication::translate("qce::spell::GrammarChecker", "The adjective does not agree with the noun");
    if (rule == "verbs")
        return QCoreApplication::translate("qce::spell::GrammarChecker", "Two verbs next to each other");
    if (rule == "rare word")
        return QCoreApplication::translate("qce::spell::GrammarChecker", "A rare word, close to a common one");
    return {};
}

// qcodeedit-grammar behind the interface: the text in UTF-8, the findings'
// byte offsets back to columns.
class BuiltinGrammar : public GrammarChecker {
public:
    QString m_language;
    grammar::Morphology m_morphology;
    std::unique_ptr<grammar::Grammar> m_grammar;

    QString language() const override { return m_language; }

    QList<GrammarIssue> check(const QString& text) const override {
        QList<GrammarIssue> issues;
        const QByteArray utf8 = text.toUtf8();
        // The column of each byte offset (of the starts of characters).
        std::vector<int> columnOf(size_t(utf8.size()) + 1, int(text.size()));
        size_t byte = 0;
        for (int column = 0; column < text.size(); ++column) {
            columnOf[byte] = column;
            const char32_t c = text[column].unicode();
            if (text[column].isHighSurrogate() && column + 1 < text.size()) {
                byte += 4;
                ++column;
            } else {
                byte += c < 0x80 ? 1 : c < 0x800 ? 2 : 3;
            }
        }
        const std::vector<grammar::Token> tokens = grammar::tokenize(std::string_view(utf8.constData(), size_t(utf8.size())));
        const auto startOf = [&](size_t token) { return columnOf[tokens[token].offset]; };
        const auto endOf = [&](size_t token) { return columnOf[tokens[token].offset + tokens[token].text.size()]; };
        for (const grammar::Finding& finding : m_grammar->check(tokens)) {
            GrammarIssue issue;
            const size_t last = finding.rule == "rare word" || finding.token + 1 >= tokens.size() ? finding.token
                                                                                                    : finding.token + 1;
            issue.start = startOf(finding.token);
            issue.end = endOf(last);
            issue.rule = QString::fromStdString(finding.rule);
            issue.message = messageFor(finding.rule);
            for (const grammar::Suggestion& s : finding.suggestions)
                issue.replacements.append({startOf(s.token), endOf(s.token), QString::fromStdString(s.word)});
            issues.append(issue);
        }
        return issues;
    }
};

// The first existing file of a name in the user's grammar directory or the installed one.
QString findFile(const QString& name, bool installedToo) {
    const QString user = QDir(grammarDataDir()).filePath(name);
    if (QFileInfo::exists(user)) return user;
    if (installedToo) {
        const QString installed = QDir(QStringLiteral(QCE_GRAMMAR_DATA_DIR)).filePath(name);
        if (QFileInfo::exists(installed)) return installed;
    }
    return {};
}

} // namespace

std::shared_ptr<GrammarChecker> builtinGrammarChecker(const QString& language) {
    const QString code = language.section(QLatin1Char('_'), 0, 0).section(QLatin1Char('-'), 0, 0).toLower();
    if (code.isEmpty()) return nullptr;
    static std::mutex mutex;
    static std::map<QString, std::weak_ptr<GrammarChecker>> loaded;
    const std::lock_guard lock(mutex);
    if (auto existing = loaded[code].lock()) return existing;
    const QString morph = findFile(code + QStringLiteral(".morph"), false);
    const QString rulesFile = findFile(code + QStringLiteral(".grammar"), true);
    if (morph.isEmpty() || rulesFile.isEmpty()) return nullptr;
    auto checker = std::make_shared<BuiltinGrammar>();
    checker->m_language = code;
    std::string error;
    grammar::GrammarRules rules;
    if (!checker->m_morphology.load(QFile::encodeName(morph).toStdString(), &error)
        || !grammar::loadGrammarRules(QFile::encodeName(rulesFile).toStdString(), rules, &error)) {
        qWarning("qcodeedit-spell: grammar for %s not loaded: %s", qPrintable(code), error.c_str());
        return nullptr;
    }
    checker->m_grammar = std::make_unique<grammar::Grammar>(checker->m_morphology);
    checker->m_grammar->setRules(std::move(rules));
    if (const QString forms = findFile(code + QStringLiteral(".forms.tsv"), false); !forms.isEmpty())
        checker->m_grammar->loadForms(QFile::encodeName(forms).toStdString());
    if (const QString complements = findFile(code + QStringLiteral(".complements.tsv"), false); !complements.isEmpty())
        checker->m_grammar->loadComplements(QFile::encodeName(complements).toStdString());
    loaded[code] = checker;
    return checker;
}

#else

std::shared_ptr<GrammarChecker> builtinGrammarChecker(const QString&) {
    return nullptr;
}

#endif

} // namespace qce::spell
