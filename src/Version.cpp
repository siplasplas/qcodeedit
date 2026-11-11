#include "qce/Version.h"

namespace qce {

// The macros as the library was built: what libraryFits() compares the
// headers of a program with.
LibraryVersion libraryVersion()
{
    return {QCE_ABI_VERSION, QCE_VERSION_MAJOR, QCE_VERSION_MINOR, QCE_VERSION_PATCH};
}

} // namespace qce
