#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxVersion.h>

#include "KateDataMigration.h"

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

namespace qce::kate {

static QString& dataDirOverride() {
    static QString s;
    return s;
}

// Set when migrateDataRoot() failed: the old place is used for this run.
static bool& useOldPlace() {
    static bool b = false;
    return b;
}

static QString oldDataRoot() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/qcodeedit");
}

QString dataRoot() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/kate-data");
}

QString dataDir() {
    if (!dataDirOverride().isEmpty()) return dataDirOverride();

    const QString env = qEnvironmentVariable("QCE_KATE_DATA_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);

    const QString version = supportedSyntaxVersion().toString();
    if (useOldPlace()) {
        const QString old = oldDataRoot();
        return old.isEmpty() ? QString() : old + QStringLiteral("/kate-") + version;
    }
    const QString root = dataRoot();
    if (root.isEmpty()) return {};
    return root + QLatin1Char('/') + version;
}

QString syntaxDir() {
    const QString d = dataDir();
    return d.isEmpty() ? QString() : d + QStringLiteral("/syntax");
}

QString themesDir() {
    const QString d = dataDir();
    return d.isEmpty() ? QString() : d + QStringLiteral("/themes");
}

void setDataDirOverride(const QString& dir) {
    dataDirOverride() = dir.isEmpty() ? QString() : QDir::cleanPath(dir);
}

DataRootMigration migrateDataRoot() {
    // An explicit data directory is the program's (or a test's) own choice.
    if (!dataDirOverride().isEmpty() || !qEnvironmentVariable("QCE_KATE_DATA_DIR").isEmpty())
        return {};
    const QString oldRoot = oldDataRoot();
    const QString newRoot = dataRoot();
    if (oldRoot.isEmpty() || newRoot.isEmpty()) return {};
    DataRootMigration result = detail::migrateKateData(oldRoot, newRoot);
    useOldPlace() = result.failed;
    return result;
}

} // namespace qce::kate
