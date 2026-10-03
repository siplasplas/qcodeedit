#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxVersion.h>

#include <QDir>
#include <QStandardPaths>
#include <QtGlobal>

namespace qce::kate {

static QString& dataDirOverride() {
    static QString s;
    return s;
}

QString dataRoot() {
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) return {};
    return base + QStringLiteral("/qcodeedit");
}

QString dataDir() {
    if (!dataDirOverride().isEmpty()) return dataDirOverride();

    const QString env = qEnvironmentVariable("QCE_KATE_DATA_DIR");
    if (!env.isEmpty()) return QDir::cleanPath(env);

    const QString root = dataRoot();
    if (root.isEmpty()) return {};
    return root + QStringLiteral("/kate-") + supportedSyntaxVersion().toString();
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

} // namespace qce::kate
