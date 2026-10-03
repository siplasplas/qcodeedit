#pragma once

#include <QString>

/// Where qcodeedit keeps downloaded Kate syntax definitions and themes.
///
/// Layout (version from supportedSyntaxVersion()):
///
///     <dataRoot>/kate-6.31/index.json
///     <dataRoot>/kate-6.31/syntax/*.xml
///     <dataRoot>/kate-6.31/themes/*.theme
///
/// dataRoot is QStandardPaths::GenericDataLocation + "/qcodeedit":
///   Linux:   ~/.local/share/qcodeedit
///   Windows: C:/Users/<user>/AppData/Local/qcodeedit
///   macOS:   ~/Library/Application Support/qcodeedit
///
/// A per-version subdirectory keeps editors built against different qcodeedit
/// versions apart; a version bump starts from an empty directory. Old kate-*
/// directories are never removed automatically.
///
/// None of these functions touch the filesystem.
namespace qce::kate {

/// GenericDataLocation + "/qcodeedit". Empty if Qt cannot determine a
/// writable location.
QString dataRoot();

/// Versioned data directory. Resolution order:
///   1. setDataDirOverride() if non-empty,
///   2. environment variable QCE_KATE_DATA_DIR if non-empty,
///   3. dataRoot() + "/kate-<major>.<minor>".
QString dataDir();

/// dataDir() + "/syntax"
QString syntaxDir();

/// dataDir() + "/themes"
QString themesDir();

/// Replace dataDir() with `dir` (used as-is, no version suffix appended).
/// Pass an empty string to clear. Not thread-safe: call during startup or in
/// tests, before other threads read the paths.
void setDataDirOverride(const QString& dir);

} // namespace qce::kate
