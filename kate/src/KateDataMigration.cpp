#include "KateDataMigration.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <qce/kate/KateSyntaxVersion.h>

namespace qce::kate::detail {

namespace {

const QDir::Filters kAllEntries = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;

bool pathExists(const QString& path) {
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

void removeEntry(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir() && !info.isSymLink())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

bool copyEntry(const QString& from, const QString& to) {
    const QFileInfo info(from);
    if (info.isSymLink()) return QFile::link(info.symLinkTarget(), to);
    if (!info.isDir()) return QFile::copy(from, to);
    if (!QDir().mkdir(to)) return false;
    for (const QString& name : QDir(from).entryList(kAllEntries)) {
        if (!copyEntry(from + QLatin1Char('/') + name, to + QLatin1Char('/') + name)) return false;
    }
    return true;
}

// Rename (atomic for a whole directory); across file systems copy, then remove
// the original after a successful copy. The target must not exist.
bool moveEntry(const QString& from, const QString& to) {
    if (pathExists(to)) return false;
    if (QDir().rename(from, to)) return true;
    if (pathExists(to) || !copyEntry(from, to)) {
        if (pathExists(from)) removeEntry(to);
        return false;
    }
    removeEntry(from);
    if (pathExists(from)) {
        removeEntry(to);
        return false;
    }
    return true;
}

// Files below dir, as paths relative to it.
QSet<QString> filesBelow(const QString& dir) {
    QSet<QString> files;
    const QDir base(dir);
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) files.insert(base.relativeFilePath(it.next()));
    return files;
}

bool sameContents(const QString& a, const QString& b) {
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) return false;
    return fa.size() == fb.size() && fa.readAll() == fb.readAll();
}

bool sameDirectory(const QString& a, const QString& b) {
    const QSet<QString> files = filesBelow(a);
    if (files != filesBelow(b)) return false;
    for (const QString& file : files) {
        if (!sameContents(a + QLatin1Char('/') + file, b + QLatin1Char('/') + file)) return false;
    }
    return true;
}

// An old copy of a version that has nothing the new copy lacks: every file is
// in the new copy with the same contents; the generated index and resource
// file may differ, and the update file may be older.
bool coveredBy(const QString& oldDir, const QString& newDir) {
    static const QRegularExpression updateFile(QStringLiteral("^update-\\d+\\.\\d+\\.xml$"));
    for (const QString& file : filesBelow(oldDir)) {
        const QString oldFile = oldDir + QLatin1Char('/') + file;
        const QString newFile = newDir + QLatin1Char('/') + file;
        if (!QFileInfo::exists(newFile)) return false;
        if (file == QLatin1String("index.json") || file == QLatin1String("theme-data.qrc")) continue;
        if (sameContents(oldFile, newFile)) continue;
        if (updateFile.match(file).hasMatch()
                && QFileInfo(oldFile).lastModified() <= QFileInfo(newFile).lastModified())
            continue;
        return false;
    }
    return true;
}

} // namespace

DataRootMigration migrateKateData(const QString& oldRoot, const QString& newRoot) {
    DataRootMigration result;
    if (!QFileInfo(oldRoot).isDir()) return result;

    static const QRegularExpression versionDir(QStringLiteral("^kate-(\\d+\\.\\d+)$"));
    static const QRegularExpression updateFile(QStringLiteral("^update-(\\d+\\.\\d+)\\.xml$"));
    struct Move { QString from, to; };
    QVector<Move> moves;
    QStringList deletions;
    auto warn = [&result](const QString& line) { result.warnings << line; };

    const QStringList entries = QDir(oldRoot).entryList(kAllEntries, QDir::Name);
    QStringList updateFiles;
    bool hasSyntaxLeftover = false;
    for (const QString& name : entries) {
        const QString path = oldRoot + QLatin1Char('/') + name;
        const auto versionMatch = versionDir.match(name);
        if (versionMatch.hasMatch() && QFileInfo(path).isDir()) {
            // kate-<version>/ -> kate-data/<version>/, every version present
            const QString target = newRoot + QLatin1Char('/') + versionMatch.captured(1);
            if (!pathExists(target))
                moves.append({path, target});
            else if (coveredBy(path, target))
                deletions << path;  // e.g. downloaded again by a program built before the move
            else
                warn(QStringLiteral("Left %1: %2 exists and differs").arg(path, target));
        } else if (updateFile.match(name).hasMatch() && QFileInfo(path).isFile()) {
            updateFiles << name;
        } else if (name == QLatin1String("syntax") && QFileInfo(path).isDir()) {
            hasSyntaxLeftover = true;
        } else {
            warn(QStringLiteral("Left %1: not recognized as Kate data").arg(path));
        }
    }

    // Leftovers of an older layout at the top: duplicates of the versioned copy
    // are deleted; without a versioned copy they become one.
    auto versionedCopy = [&](const QString& version) {
        const QString moved = newRoot + QLatin1Char('/') + version;
        if (pathExists(moved)) return moved;
        const QString old = oldRoot + QStringLiteral("/kate-") + version;
        return pathExists(old) ? old : QString();
    };
    auto leftover = [&](const QString& name, const QString& version, bool directory) {
        const QString path = oldRoot + QLatin1Char('/') + name;
        const QString target = newRoot + QLatin1Char('/') + version + QLatin1Char('/') + name;
        const QString copy = versionedCopy(version);
        if (copy.isEmpty()) {
            moves.append({path, target});
            return;
        }
        const QString counterpart = copy + QLatin1Char('/') + name;
        const bool same = directory ? QFileInfo(counterpart).isDir() && sameDirectory(path, counterpart)
                                    : QFileInfo(counterpart).isFile() && sameContents(path, counterpart);
        if (same)
            deletions << path;
        else  // named by where the versioned copy is after the move
            warn(QStringLiteral("Left %1: differs from %2, which is kept").arg(path, target));
    };
    for (const QString& name : updateFiles)
        leftover(name, updateFile.match(name).captured(1), false);
    if (hasSyntaxLeftover) {
        // The version comes from the update file beside it, if there is just one.
        const QString version = updateFiles.size() == 1 ? updateFile.match(updateFiles.first()).captured(1)
                                                        : supportedSyntaxVersion().toString();
        leftover(QStringLiteral("syntax"), version, true);
    }

    QVector<Move> done;
    QStringList created;
    for (const Move& m : moves) {
        QStringList missing;
        for (QString dir = QFileInfo(m.to).absolutePath(); !pathExists(dir); dir = QFileInfo(dir).absolutePath())
            missing.prepend(dir);
        bool ok = true;
        for (const QString& dir : missing) {
            if (!QDir().mkdir(dir)) { ok = false; break; }
            created << dir;
        }
        if (ok && moveEntry(m.from, m.to)) {
            done << m;
            result.log << QStringLiteral("Moved %1 to %2").arg(m.from, m.to);
            continue;
        }
        if (pathExists(m.to) && pathExists(m.from)) {
            // Another program moved it meanwhile; the old copy is handled next time.
            warn(QStringLiteral("Left %1: %2 appeared meanwhile").arg(m.from, m.to));
            continue;
        }
        // Back to the old place, used for this run; the next start tries again.
        result.failed = true;
        warn(QStringLiteral("Could not move %1 to %2").arg(m.from, m.to));
        for (int i = done.size() - 1; i >= 0; --i) {
            if (!moveEntry(done.at(i).to, done.at(i).from))
                warn(QStringLiteral("Could not move %1 back from %2").arg(done.at(i).from, done.at(i).to));
        }
        for (int i = created.size() - 1; i >= 0; --i) QDir().rmdir(created.at(i));
        result.log.clear();
        return result;
    }

    for (const QString& path : deletions) {
        removeEntry(path);
        result.log << QStringLiteral("Removed %1, a duplicate of the data in %2").arg(path, newRoot);
    }
    const QStringList rest = QDir(oldRoot).entryList(kAllEntries, QDir::Name);
    if (rest.isEmpty() && QDir().rmdir(oldRoot))
        result.log << QStringLiteral("Removed the empty %1").arg(oldRoot);
    else if (!rest.isEmpty())
        warn(QStringLiteral("Left %1, which still holds: %2").arg(oldRoot, rest.join(QStringLiteral(", "))));
    return result;
}

} // namespace qce::kate::detail
