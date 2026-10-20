// spelltune: fits the costs and limits of the spelling suggestions
// (spell/src/FormIndex.h) to real typing mistakes.
//
// Each pairs file has lines "typed<TAB>right[<TAB>kind]" (tools/spelltune/
// pairs.py makes them from a text as typed and the same text corrected).
// The score of a setting rewards what a writer wants from the suggestions:
// one suggestion, the right one; or the right one among a few; it punishes
// suggestions without the right word, no suggestion at all, and above all a
// sure correction (applied while typing) that is wrong. The parameters are
// changed one at a time, up and down, a change kept when the training score
// improves, until none does; the check sets show whether that holds on text
// not tuned on. The result is printed as the lines to put in FormIndex.h.
//
// Usage: spelltune [--layout pl-windows] [--rounds 4]
//                  --train pairs.tsv:pl[:odd|even] ... --check pairs.tsv:pl[:odd|even] ...

#include "FormIndex.h"

#include <qce/spell/SpellChecker.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QThread>

#include <cstdio>
#include <memory>
#include <vector>

using namespace qce::spell;
using namespace qce::spell::detail;

namespace {

struct Pair {
    QString typed, right;
};

struct Set {
    QString name, language;
    std::vector<Pair> pairs;
};

struct Result {
    int flagged = 0, single = 0, firstOfSeveral = 0, laterOfSeveral = 0, withoutRight = 0, none = 0, sure = 0,
        sureWrong = 0;
    double score() const {
        return 1.0 * single + 0.7 * firstOfSeveral + 0.4 * laterOfSeveral - 1.0 * withoutRight - 0.5 * none
            - 5.0 * sureWrong;
    }
};

// file:language[:odd|even]
Set readSet(const QString& spec) {
    const QStringList parts = spec.split(QLatin1Char(':'));
    Set set;
    set.name = spec;
    set.language = parts.value(1, QStringLiteral("pl"));
    const QString half = parts.value(2);
    QFile file(parts.value(0));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot read %s\n", qPrintable(parts.value(0)));
        return set;
    }
    int line = 0;
    for (const QString& text : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const bool odd = line++ % 2;
        if ((half == QLatin1String("odd") && !odd) || (half == QLatin1String("even") && odd)) continue;
        const QStringList cols = text.split(QLatin1Char('\t'));
        if (cols.size() >= 2) set.pairs.push_back({cols[0], cols[1]});
    }
    return set;
}

// Both kinds of apostrophe as one, either case of the first letter.
bool same(const QString& a, const QString& b) {
    QString x = a, y = b;
    x.replace(QChar(0x2019), QLatin1Char('\''));
    y.replace(QChar(0x2019), QLatin1Char('\''));
    return x.compare(y, Qt::CaseInsensitive) == 0;
}

Result evaluate(const Set& set) {
    // A new checker: no suggestions kept from another setting.
    SpellChecker checker;
    checker.setLanguages({set.language});
    for (int i = 0; i < 1200 && !checker.suggestionIndexReady(); ++i) QThread::msleep(100);
    Result r;
    for (const Pair& pair : set.pairs) {
        if (checker.check(pair.typed)) continue;
        ++r.flagged;
        const auto suggestions = checker.scoredSuggestions(pair.typed);
        int at = -1;
        for (int i = 0; i < suggestions.size(); ++i)
            if (same(suggestions[i].text, pair.right)) {
                at = i;
                break;
            }
        if (suggestions.isEmpty()) ++r.none;
        else if (at < 0) ++r.withoutRight;
        else if (suggestions.size() == 1) ++r.single;
        else if (at == 0) ++r.firstOfSeveral;
        else ++r.laterOfSeveral;
        if (SpellChecker::isConfident(pair.typed, suggestions)) {
            ++r.sure;
            r.sureWrong += at != 0;
        }
    }
    return r;
}

struct Parameter {
    const char* name;
    double* value;
    double step;
};

std::vector<Parameter> parameters() {
    return {
        {"kInsertCost", &kInsertCost, 0.1},
        {"kInsertAtStartCost", &kInsertAtStartCost, 0.1},
        {"kDeleteCost", &kDeleteCost, 0.1},
        {"kDeleteTouchingCost", &kDeleteTouchingCost, 0.1},
        {"kDeleteTouchingOneCost", &kDeleteTouchingOneCost, 0.1},
        {"kMissingDiacriticCost", &kMissingDiacriticCost, 0.05},
        {"kTypedDiacriticCost", &kTypedDiacriticCost, 0.1},
        {"kMovedDiacriticCost", &kMovedDiacriticCost, 0.1},
        {"kNeighbourKeyCost", &kNeighbourKeyCost, 0.1},
        {"kOtherLetterCost", &kOtherLetterCost, 0.1},
        {"kSwapTwoHandsCost", &kSwapTwoHandsCost, 0.1},
        {"kSwapOneHandCost", &kSwapOneHandCost, 0.1},
        {"kMoveCost[2]", &kMoveCost[2], 0.1},
        {"kMoveCost[3]", &kMoveCost[3], 0.1},
        {"kMoveAgainCost", &kMoveAgainCost, 0.1},
        {"kShiftHeldCost", &kShiftHeldCost, 0.05},
        {"kInnerCapitalCost", &kInnerCapitalCost, 0.1},
        {"kSplitCost", &kSplitCost, 0.1},
        {"kNewCapitalCost", &kNewCapitalCost, 0.1},
        {"kFrequencyWeight", &kFrequencyWeight, 0.05},
        {"kCloseToBest", &kCloseToBest, 0.1},
        {"kCloseToBestWithFrequency", &kCloseToBestWithFrequency, 0.1},
        {"kRivalPenaltyGap", &kRivalPenaltyGap, 0.1},
        {"kShownLimit", &kShownLimit, 0.2},
        {"kFallbackShownLimit", &kFallbackShownLimit, 0.2},
        {"kConfidentDistance", &kConfidentDistance, 0.1},
        {"kConfidentScore", &kConfidentScore, 0.1},
    };
}

double total(const std::vector<Set>& sets, std::vector<Result>* results = nullptr) {
    // Candidates are looked for as far as anything may be shown.
    kSuggestionLimit = std::max(kShownLimit, kFallbackShownLimit);
    double sum = 0;
    if (results) results->clear();
    for (const Set& set : sets) {
        const Result r = evaluate(set);
        sum += r.score();
        if (results) results->push_back(r);
    }
    return sum;
}

void report(const char* title, const std::vector<Set>& sets) {
    std::vector<Result> results;
    total(sets, &results);
    std::printf("%s\n", title);
    std::printf("  %-44s %5s %6s %5s %5s %5s %5s %5s %5s %7s\n", "set", "found", "single", "first", "later",
                "wrong", "none", "sure", "sureX", "score");
    for (size_t i = 0; i < sets.size(); ++i) {
        const Result& r = results[i];
        std::printf("  %-44s %5d %6d %5d %5d %5d %5d %5d %5d %7.1f\n", qPrintable(sets[i].name.right(44)), r.flagged,
                    r.single, r.firstOfSeveral, r.laterOfSeveral, r.withoutRight, r.none, r.sure, r.sureWrong,
                    r.score());
    }
    std::fflush(stdout);
}

// The values of all parameters, in parameters()' order.
using Setting = std::vector<double>;

Setting current() {
    Setting values;
    for (const Parameter& p : parameters()) values.push_back(*p.value);
    return values;
}

void apply(const Setting& values) {
    const std::vector<Parameter> list = parameters();
    for (size_t i = 0; i < list.size() && i < values.size(); ++i) *list[i].value = values[i];
}

QStringList settingArguments(const Setting& values) {
    QStringList args;
    const std::vector<Parameter> list = parameters();
    for (size_t i = 0; i < list.size(); ++i)
        args << QStringLiteral("--set") << QStringLiteral("%1=%2").arg(QLatin1String(list[i].name)).arg(values[i], 0, 'g', 17);
    return args;
}

bool setParameter(const QString& assignment) {
    const QString name = assignment.section(QLatin1Char('='), 0, 0);
    bool ok = false;
    const double value = assignment.section(QLatin1Char('='), 1).toDouble(&ok);
    for (const Parameter& p : parameters())
        if (ok && name == QLatin1String(p.name)) {
            *p.value = value;
            return true;
        }
    std::fprintf(stderr, "unknown or bad setting %s\n", qPrintable(assignment));
    return false;
}

// The training score of each setting, worked out by `jobs` processes of this
// program at once (one setting each: hunspell is not for threads).
std::vector<double> scores(const std::vector<Setting>& settings, const QStringList& common, int jobs) {
    std::vector<double> result(settings.size(), -1e18);
    std::vector<std::pair<std::unique_ptr<QProcess>, size_t>> running;
    size_t next = 0;
    while (next < settings.size() || !running.empty()) {
        while (next < settings.size() && int(running.size()) < jobs) {
            auto process = std::make_unique<QProcess>();
            process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            process->start(QCoreApplication::applicationFilePath(),
                           QStringList{QStringLiteral("--worker")} + common + settingArguments(settings[next]));
            running.emplace_back(std::move(process), next++);
        }
        for (auto it = running.begin(); it != running.end();) {
            if (it->first->state() != QProcess::NotRunning && !it->first->waitForFinished(20)) {
                ++it;
                continue;
            }
            const QString out = QString::fromUtf8(it->first->readAllStandardOutput());
            const qsizetype at = out.lastIndexOf(QLatin1String("SCORE "));
            if (at >= 0) result[it->second] = out.mid(at + 6).trimmed().toDouble();
            it = running.erase(it);
        }
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    std::vector<Set> train, check;
    int rounds = 20;
    int jobs = QThread::idealThreadCount();
    bool worker = false;
    QStringList common;  // what every worker gets: layout and training sets
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const bool hasValue = i + 1 < args.size();
        if (args[i] == QLatin1String("--worker")) worker = true;
        else if (args[i] == QLatin1String("--layout") && hasValue) {
            qce::spell::setKeyboardLayout(args[++i]);
            common << args[i - 1] << args[i];
        } else if (args[i] == QLatin1String("--rounds") && hasValue) rounds = args[++i].toInt();
        else if (args[i] == QLatin1String("--jobs") && hasValue) jobs = std::max(1, args[++i].toInt());
        else if (args[i] == QLatin1String("--set") && hasValue) {
            if (!setParameter(args[++i])) return 1;
        } else if (args[i] == QLatin1String("--train") && hasValue) {
            train.push_back(readSet(args[++i]));
            common << args[i - 1] << args[i];
        } else if (args[i] == QLatin1String("--check") && hasValue) check.push_back(readSet(args[++i]));
    }
    if (worker) {
        std::printf("SCORE %.6f\n", total(train));
        return 0;
    }
    if (train.empty()) {
        std::fprintf(stderr, "usage: spelltune [--layout L] [--rounds N] [--jobs N] [--set name=value ...]\n"
                             "                 --train pairs:lang[:odd|even] ... [--check pairs:lang[:odd|even] ...]\n");
        return 1;
    }
    std::printf("layout %s, %d jobs\n", qPrintable(qce::spell::keyboardLayoutFor(train.front().language)), jobs);
    report("before, training sets:", train);
    if (!check.empty()) report("before, check sets:", check);

    QElapsedTimer timer;
    timer.start();
    const std::vector<Parameter> list = parameters();
    Setting best = current();
    double bestScore = scores({best}, common, 1).front();
    for (int round = 0; round < rounds; ++round) {
        // Every parameter one and two steps down and up, all at once.
        std::vector<Setting> candidates;
        std::vector<size_t> changed;
        for (size_t i = 0; i < list.size(); ++i)
            for (const double delta : {-2 * list[i].step, -list[i].step, list[i].step, 2 * list[i].step}) {
                Setting s = best;
                s[i] = best[i] + delta;
                if (s[i] < 0) continue;
                candidates.push_back(s);
                changed.push_back(i);
            }
        const std::vector<double> got = scores(candidates, common, jobs);
        // The best value of each parameter that improves on its own.
        std::vector<int> pick(list.size(), -1);
        size_t top = 0;
        for (size_t c = 0; c < candidates.size(); ++c) {
            if (got[c] > got[top]) top = c;
            if (got[c] <= bestScore + 1e-9) continue;
            int& p = pick[changed[c]];
            if (p < 0 || got[c] > got[size_t(p)]) p = int(c);
        }
        if (got.empty() || got[top] <= bestScore + 1e-9) break;
        // Those changes together, unless they do worse than the best alone.
        Setting together = best;
        int count = 0;
        for (size_t i = 0; i < list.size(); ++i)
            if (pick[i] >= 0) {
                together[i] = candidates[size_t(pick[i])][i];
                ++count;
            }
        const double togetherScore = count > 1 ? scores({together}, common, 1).front() : -1e18;
        const Setting previous = best;
        if (togetherScore >= got[top]) {
            best = together;
            bestScore = togetherScore;
        } else {
            best = candidates[top];
            bestScore = got[top];
        }
        std::printf("round %d (%.0f s): score %.1f;", round + 1, timer.elapsed() / 1000.0, bestScore);
        for (size_t i = 0; i < list.size(); ++i)
            if (best[i] != previous[i]) std::printf(" %s %.2f -> %.2f", list[i].name, previous[i], best[i]);
        std::printf("\n");
        std::fflush(stdout);
    }
    apply(best);
    report("after, training sets:", train);
    if (!check.empty()) report("after, check sets:", check);
    std::printf("values:\n");
    for (const Parameter& p : list) std::printf("  %s = %.2f;\n", p.name, *p.value);
    return 0;
}
