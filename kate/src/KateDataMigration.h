#pragma once

#include <qce/kate/KatePaths.h>

namespace qce::kate::detail {

/// migrateDataRoot() with explicit places: moves the Kate data of the old
/// layout under @p oldRoot (kate-<version>/, and older leftovers syntax/ and
/// update-<version>.xml at the top) to @p newRoot/<version>/.
DataRootMigration migrateKateData(const QString& oldRoot, const QString& newRoot);

} // namespace qce::kate::detail
