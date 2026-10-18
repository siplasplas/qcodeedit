#pragma once

#include <qce/LineDiff.h>

namespace qce::diff {

/// The lines that differ between two texts, found by DiffMerge's libdiffcore
/// (exact comparison, adjacent removals and insertions joined).
QVector<LineChange> lineChanges(const QStringList& oldLines, const QStringList& newLines);

/// Makes SimpleTextDocument::setText() change only the lines that differ
/// (qce::setLineDiff(lineChanges)). Called by itself in every program linked
/// with qcodeedit::diff, before main().
void install();

} // namespace qce::diff
