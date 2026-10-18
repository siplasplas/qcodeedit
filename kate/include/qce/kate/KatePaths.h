#pragma once

#include <QString>
#include <QStringList>

/// Where downloaded Kate syntax definitions and themes are kept.
///
/// The data is in KDE's format and usable by any program reading it, so it is
/// kept in a neutral, shared place rather than under one program's directory.
/// Layout (version from supportedSyntaxVersion()):
///
///     <dataRoot>/6.31/index.json
///     <dataRoot>/6.31/syntax/*.xml
///     <dataRoot>/6.31/themes/*.theme
///     <dataRoot>/6.31/theme-data.qrc
///     <dataRoot>/6.31/update-6.31.xml
///
/// dataRoot is QStandardPaths::GenericDataLocation + "/kate-data":
///   Linux:   ~/.local/share/kate-data
///   Windows: C:/Users/<user>/AppData/Local/kate-data
///   macOS:   ~/Library/Application Support/kate-data
///
/// A per-version subdirectory keeps programs built against different qcodeedit
/// versions apart; a version bump starts from an empty directory. Old version
/// directories are never removed automatically.
///
/// Up to qcodeedit 1.12 the data was in GenericDataLocation + "/qcodeedit/kate-<version>";
/// migrateDataRoot() moves it. Apart from that function nothing here touches
/// the filesystem.
namespace qce::kate {

/// GenericDataLocation + "/kate-data". Empty if Qt cannot determine a
/// writable location.
QString dataRoot();

/// Versioned data directory. Resolution order:
///   1. setDataDirOverride() if non-empty,
///   2. environment variable QCE_KATE_DATA_DIR if non-empty,
///   3. dataRoot() + "/<major>.<minor>"; after a failed migrateDataRoot(),
///      the old place for the rest of the run.
QString dataDir();

/// dataDir() + "/syntax"
QString syntaxDir();

/// dataDir() + "/themes"
QString themesDir();

/// Replace dataDir() with `dir` (used as-is, no version suffix appended).
/// Pass an empty string to clear. Not thread-safe: call during startup or in
/// tests, before other threads read the paths.
void setDataDirOverride(const QString& dir);

/// Result of migrateDataRoot().
struct DataRootMigration {
    bool failed = false;   ///< nothing moved; dataDir() uses the old place for this run
    QStringList log;       ///< what was done, one line per action
    QStringList warnings;  ///< what was left in the old place and why
};

/// Moves Kate data from the place used up to qcodeedit 1.12,
/// GenericDataLocation/qcodeedit, to dataRoot(). A program calls it once at
/// start-up, before anything reads the data, and reports the result itself.
///
/// - Every kate-<version>/ directory becomes dataRoot()/<version>/ (renamed
///   whole; across file systems copied, then removed).
/// - An existing dataRoot()/<version>/ is never overwritten. An old copy with
///   nothing the new one lacks (e.g. downloaded again by a program built
///   before the move) is deleted; otherwise it stays with a warning.
/// - Leftovers of an older layout at the top (syntax/, update-<version>.xml)
///   are deleted when the versioned copy holds the same files, or become the
///   versioned copy when there is none.
/// - Anything else stays with a warning; the old directory is removed when
///   empty. On a failure everything moved is moved back.
///
/// Does nothing with setDataDirOverride() or QCE_KATE_DATA_DIR in effect, or
/// when there is no old place. Not thread-safe, like setDataDirOverride().
DataRootMigration migrateDataRoot();

} // namespace qce::kate
