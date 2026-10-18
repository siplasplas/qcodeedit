// Compiled into every target that links qcodeedit::diff (an interface
// source), so that linking the library is enough: setText() then changes
// only the lines that differ, without any call in the program.

#include <qce/diff/LineDiff.h>

namespace {
const bool qceDiffInstalled = (qce::diff::install(), true);
}
