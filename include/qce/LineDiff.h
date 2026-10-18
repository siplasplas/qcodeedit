#pragma once

#include <QStringList>
#include <QVector>

namespace qce {

/// A run of lines that differs between two texts: `oldCount` lines from
/// `oldStart` of the old text become `newCount` lines from `newStart` of the
/// new one (either count may be 0).
struct LineChange {
    int oldStart = 0;
    int oldCount = 0;
    int newStart = 0;
    int newCount = 0;
};

/// Compares the lines of two texts; returns the changes in order, none when
/// the texts are equal.
using LineDiffFn = QVector<LineChange> (*)(const QStringList& oldLines, const QStringList& newLines);

/// The comparison SimpleTextDocument::setText() uses to change only the
/// lines that differ, which CodeEditArea records as one undo step. None by
/// default: setText() then replaces the whole text and the undo history is
/// cleared. qcodeedit-diff (DiffMerge's libdiffcore) sets it in every
/// program linked with it.
void setLineDiff(LineDiffFn fn);
LineDiffFn lineDiff();

} // namespace qce
