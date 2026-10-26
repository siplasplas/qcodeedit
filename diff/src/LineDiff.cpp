#include <qce/diff/LineDiff.h>

#include <diffcore/DiffEngine.h>

#include <QHash>

#include <algorithm>

namespace qce::diff {

namespace {

// libdiffcore takes about 1.6e-9 * D² seconds for D changed lines (both
// sides, release build): 0.2 s for 9 000, 1.4 s for 27 000, 12.6 s for
// 90 000, two minutes for a file of 130 000 lines all changed (a new
// column). Beyond this many the texts are not compared line by line.
constexpr qsizetype kMaxEdits = 16000;

} // namespace

QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines) {
    // Too many changes: the lines between the common start and end as one
    // change, found in linear time. The edits are at least the lines of
    // either side that the other lacks (counted with a hash of the lines
    // between), so the estimate never takes a long comparison for a short one.
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
    if (oldMiddle + newMiddle > kMaxEdits) {
        QHash<QString, qsizetype> counts;
        counts.reserve(oldMiddle);
        for (qsizetype i = prefix; i < prefix + oldMiddle; ++i) ++counts[oldLines[i]];
        qsizetype matched = 0;
        for (qsizetype i = prefix; i < prefix + newMiddle; ++i) {
            const auto it = counts.find(newLines[i]);
            if (it != counts.end() && it.value() > 0) {
                --it.value();
                ++matched;
            }
        }
        if (oldMiddle + newMiddle - 2 * matched > kMaxEdits)
            return {{int(prefix), int(oldMiddle), int(prefix), int(newMiddle)}};
    }
    diffcore::DiffOptions options;
    // Lines come without line breaks; every difference counts.
    options.ignoreEolStyle = false;
    const diffcore::DiffResult result = diffcore::DiffEngine().compute(oldLines, newLines, options);
    QVector<LineChange> changes;
    for (const diffcore::Hunk& hunk : result.hunks) {
        if (hunk.type == diffcore::ChangeType::Equal) continue;
        changes.append({hunk.leftRange.start, hunk.leftRange.count,
                        hunk.rightRange.start, hunk.rightRange.count});
    }
    return changes;
}

void install() {
    setLineDiff(&lineChanges);
}

} // namespace qce::diff
