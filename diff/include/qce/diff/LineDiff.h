#pragma once

#include <qce/LineDiff.h>

namespace qce::diff {

/// The lines that differ between two texts, found by DiffMerge's libdiffcore
/// (exact comparison, adjacent removals and insertions joined). With too
/// many changes to compare in a moment (more than 16 000 lines that one side
/// has and the other lacks, e.g. every line of a large file changed), the
/// lines between the common start and end are one change, found at once.
QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines);

/// Makes SimpleTextDocument::setText() change only the lines that differ
/// (qce::setLineDiff(lineChanges)). Called by itself in every program linked
/// with qcodeedit::diff, before main().
void install();

} // namespace qce::diff
