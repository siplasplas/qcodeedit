#pragma once

#include <qce/LineDiff.h>

namespace qce::diff {

/// The lines that differ between two texts, found by DiffMerge's libdiffcore
/// (exact comparison, adjacent removals and insertions joined). With too
/// many changes for the length to compare in a moment (the algorithm takes
/// N·P steps, N the lines, P those changed; beyond 10⁹, about half a second,
/// e.g. every line of a large file changed), the lines between the common
/// start and end are one change, found at once.
QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines);

/// Makes SimpleTextDocument::setText() change only the lines that differ
/// (qce::setLineDiff(lineChanges)). Called by itself in every program linked
/// with qcodeedit::diff, before main().
void install();

} // namespace qce::diff
