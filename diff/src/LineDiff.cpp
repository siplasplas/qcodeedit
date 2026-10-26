#include <qce/diff/LineDiff.h>

#include <diffcore/DiffEngine.h>

#include <QHash>

#include <algorithm>

namespace qce::diff {

namespace {

// libdiffcore is Wu, Manber, Myers and Miller's O(NP) algorithm: N the
// longer sequence, P the deletions of the shortest edit script (a changed
// line is a deletion and an insertion). Measured on 135 000 lines (release
// build): N·P of 6·10⁸ took 0.2 s, 1.8·10⁹ 1.4 s, 6·10⁹ 12.6 s. Beyond this
// the texts are not compared line by line (about half a second).
constexpr double kMaxSteps = 1e9;

} // namespace

QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines) {
    // The common start and end need no comparing: only the lines between go
    // to libdiffcore, so that its N is theirs.
    const qsizetype shorter = std::min(oldLines.size(), newLines.size());
    qsizetype prefix = 0;
    while (prefix < shorter && oldLines[prefix] == newLines[prefix]) ++prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix
           && oldLines[oldLines.size() - 1 - suffix] == newLines[newLines.size() - 1 - suffix])
        ++suffix;
    const qsizetype oldMiddle = oldLines.size() - prefix - suffix;
    const qsizetype newMiddle = newLines.size() - prefix - suffix;
    if (oldMiddle == 0 && newMiddle == 0) return {};
    const QStringList oldPart = oldLines.mid(prefix, oldMiddle), newPart = newLines.mid(prefix, newMiddle);
    // Too many steps: the lines between as one change, found in linear time.
    // P is at least the lines of the shorter side that the longer one lacks
    // (counted with a hash), so the estimate never stops a quick comparison;
    // it is counted only when N times the largest P could pass the limit.
    const qsizetype n = std::max(oldMiddle, newMiddle);
    const qsizetype m = std::min(oldMiddle, newMiddle);
    if (double(n) * double(m) > kMaxSteps) {
        QHash<QString, qsizetype> counts;
        counts.reserve(oldMiddle);
        for (const QString& line : oldPart) ++counts[line];
        qsizetype matched = 0;
        for (const QString& line : newPart) {
            const auto it = counts.find(line);
            if (it != counts.end() && it.value() > 0) {
                --it.value();
                ++matched;
            }
        }
        const qsizetype p = m - matched;
        if (double(n) * double(p) > kMaxSteps) return {{int(prefix), int(oldMiddle), int(prefix), int(newMiddle)}};
    }
    diffcore::DiffOptions options;
    // Lines come without line breaks; every difference counts.
    options.ignoreEolStyle = false;
    const diffcore::DiffResult result = diffcore::DiffEngine().compute(oldPart, newPart, options);
    QVector<LineChange> changes;
    for (const diffcore::Hunk& hunk : result.hunks) {
        if (hunk.type == diffcore::ChangeType::Equal) continue;
        changes.append({int(prefix) + hunk.leftRange.start, hunk.leftRange.count,
                        int(prefix) + hunk.rightRange.start, hunk.rightRange.count});
    }
    return changes;
}

void install() {
    setLineDiff(&lineChanges);
}

} // namespace qce::diff
