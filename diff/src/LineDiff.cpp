#include <qce/diff/LineDiff.h>

#include <diffcore/DiffEngine.h>

namespace qce::diff {

QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines) {
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
