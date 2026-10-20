#include "UserStore.h"

#include <qce/spell/SpellChecker.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <sqlite3.h>

#include <initializer_list>

namespace qce::spell::detail {

namespace {

// A prepared statement with text parameters, finalized at the end of scope.
class Statement {
public:
    Statement(sqlite3* db, const char* sql, std::initializer_list<QString> values) {
        if (!db || sqlite3_prepare_v2(db, sql, -1, &m_statement, nullptr) != SQLITE_OK) {
            m_statement = nullptr;
            return;
        }
        int index = 1;
        for (const QString& value : values) {
            const QByteArray text = value.toUtf8();
            sqlite3_bind_text(m_statement, index++, text.constData(), int(text.size()), SQLITE_TRANSIENT);
        }
    }
    ~Statement() {
        if (m_statement) sqlite3_finalize(m_statement);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    /// The next row; false at the end (or on an error).
    bool next() { return m_statement && sqlite3_step(m_statement) == SQLITE_ROW; }
    /// Runs a statement without rows; true when it worked.
    bool run() { return m_statement && sqlite3_step(m_statement) == SQLITE_DONE; }
    QString text(int column) const {
        const auto* bytes = reinterpret_cast<const char*>(sqlite3_column_text(m_statement, column));
        return bytes ? QString::fromUtf8(bytes, sqlite3_column_bytes(m_statement, column)) : QString();
    }
    qint64 integer(int column) const { return sqlite3_column_int64(m_statement, column); }

private:
    sqlite3_stmt* m_statement = nullptr;
};

// A file and the files under it when it is a directory: "path = ?1 OR
// (path >= ?2 AND path < ?3)" with "dir/" and "dir0" ('0' follows '/'), a
// range of the sorted paths, so the index finds it at once.
QString underLower(const QString& path) { return path + QLatin1Char('/'); }
QString underUpper(const QString& path) { return path + QLatin1Char('0'); }
#define QCE_FILE_OR_UNDER "(path = ?1 OR (path >= ?2 AND path < ?3))"

// SQLite counts characters (code points), QString UTF-16 units.
QString afterPrefix(const QString& path) { return QString::number(path.toUcs4().size() + 1); }

// Without a trailing slash (the root stays "/").
QString normalized(const QString& path) {
    QString result = path;
    while (result.size() > 1 && result.endsWith(QLatin1Char('/'))) result.chop(1);
    return result;
}

constexpr int kSchemaVersion = 1;

} // namespace

UserStore& UserStore::instance() {
    static UserStore store;
    return store;
}

UserStore::UserStore() {
    const QString path = userDataPath();
    if (path.isEmpty()) return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (sqlite3_open(QFile::encodeName(path).constData(), &m_db) != SQLITE_OK) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    // Several programs at once: wait for each other a moment.
    sqlite3_busy_timeout(m_db, 3000);
    exec("PRAGMA journal_mode=WAL");
    createSchema();
    importOldFiles(QFileInfo(path).absolutePath());
}

UserStore::~UserStore() {
    if (m_db) sqlite3_close(m_db);
}

void UserStore::createSchema() {
    // One program at a time: another may be migrating the same file.
    exec("BEGIN IMMEDIATE");
    // Statements are finished before the tables change.
    const int current = [this]() {
        Statement version(m_db, "PRAGMA user_version", {});
        return version.next() ? int(version.integer(0)) : 0;
    }();
    if (current >= kSchemaVersion) {
        exec("COMMIT");
        return;
    }
    // Version 0 with tables: 1.46 and 1.47, a rowid table and an index for
    // each, and each ignored word with its whole path.
    const bool migrate =
        Statement(m_db, "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'words'", {}).next();
    if (migrate) {
        exec("ALTER TABLE words RENAME TO words_old");
        exec("ALTER TABLE corrections RENAME TO corrections_old");
        exec("ALTER TABLE ignored RENAME TO ignored_old");
    }
    // Short rows whose key is all they hold: kept once, in the key's order.
    exec("CREATE TABLE words(language TEXT NOT NULL, word TEXT NOT NULL, PRIMARY KEY(language, word))"
         " WITHOUT ROWID");
    exec("CREATE TABLE corrections(language TEXT NOT NULL, typed TEXT NOT NULL, correction TEXT NOT NULL,"
         " PRIMARY KEY(language, typed)) WITHOUT ROWID");
    // Each file's path once, sorted by its index: a moved directory changes
    // only the paths under it, found as a range.
    exec("CREATE TABLE files(id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE)");
    exec("CREATE TABLE ignored(file INTEGER NOT NULL REFERENCES files(id), word TEXT NOT NULL,"
         " PRIMARY KEY(file, word)) WITHOUT ROWID");
    if (migrate) {
        exec("INSERT OR IGNORE INTO words SELECT language, word FROM words_old");
        exec("INSERT OR IGNORE INTO corrections SELECT language, typed, correction FROM corrections_old");
        exec("INSERT OR IGNORE INTO files(path) SELECT DISTINCT path FROM ignored_old");
        exec("INSERT OR IGNORE INTO ignored SELECT files.id, ignored_old.word FROM ignored_old"
             " JOIN files ON files.path = ignored_old.path");
        exec("DROP TABLE words_old");
        exec("DROP TABLE corrections_old");
        exec("DROP TABLE ignored_old");
    }
    exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion).toLatin1().constData());
    if (!exec("COMMIT")) exec("ROLLBACK");
}

bool UserStore::exec(const char* sql) {
    return m_db && sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

qint64 UserStore::stamp() {
    // data_version changes when another connection commits; ours count here.
    Statement version(m_db, "PRAGMA data_version", {});
    const qint64 others = version.next() ? version.integer(0) : 0;
    return (others << 32) + m_ownChanges;
}

QSet<QString> UserStore::words(const QString& language) {
    QSet<QString> result;
    Statement select(m_db, "SELECT word FROM words WHERE language = ?", {language});
    while (select.next()) result.insert(select.text(0));
    return result;
}

void UserStore::addWord(const QString& language, const QString& word) {
    if (Statement(m_db, "INSERT OR IGNORE INTO words(language, word) VALUES(?, ?)", {language, word}).run())
        ++m_ownChanges;
}

void UserStore::removeWord(const QString& language, const QString& word) {
    if (Statement(m_db, "DELETE FROM words WHERE language = ? AND word = ?", {language, word}).run()) ++m_ownChanges;
}

QHash<QString, QString> UserStore::corrections(const QString& language) {
    QHash<QString, QString> result;
    Statement select(m_db, "SELECT typed, correction FROM corrections WHERE language = ?", {language});
    while (select.next()) result.insert(select.text(0), select.text(1));
    return result;
}

void UserStore::addCorrection(const QString& language, const QString& typed, const QString& correction) {
    if (Statement(m_db, "INSERT OR REPLACE INTO corrections(language, typed, correction) VALUES(?, ?, ?)",
                  {language, typed, correction}).run())
        ++m_ownChanges;
}

void UserStore::removeCorrection(const QString& language, const QString& typed) {
    if (Statement(m_db, "DELETE FROM corrections WHERE language = ? AND typed = ?", {language, typed}).run())
        ++m_ownChanges;
}

QSet<QString> UserStore::ignored(const QString& path) {
    QSet<QString> result;
    Statement select(m_db, "SELECT word FROM ignored JOIN files ON files.id = ignored.file WHERE files.path = ?",
                     {normalized(path)});
    while (select.next()) result.insert(select.text(0));
    return result;
}

void UserStore::addIgnored(const QString& path, const QString& word) {
    const QString file = normalized(path);
    exec("BEGIN");
    Statement(m_db, "INSERT OR IGNORE INTO files(path) VALUES(?)", {file}).run();
    const bool added = Statement(m_db, "INSERT OR IGNORE INTO ignored(file, word) SELECT id, ?2 FROM files"
                                       " WHERE path = ?1", {file, word}).run();
    if (exec("COMMIT") && added) ++m_ownChanges;
}

void UserStore::removeIgnored(const QString& path, const QString& word) {
    const QString file = normalized(path);
    exec("BEGIN");
    Statement(m_db, "DELETE FROM ignored WHERE word = ?2 AND file = (SELECT id FROM files WHERE path = ?1)",
              {file, word}).run();
    // A file without ignored words is not kept.
    Statement(m_db, "DELETE FROM files WHERE path = ? AND NOT EXISTS"
                    " (SELECT 1 FROM ignored WHERE ignored.file = files.id)", {file}).run();
    if (exec("COMMIT")) ++m_ownChanges;
}

QList<QPair<QString, QString>> UserStore::allIgnored() {
    QList<QPair<QString, QString>> result;
    Statement select(m_db, "SELECT files.path, ignored.word FROM files JOIN ignored ON ignored.file = files.id"
                           " ORDER BY files.path, ignored.word", {});
    while (select.next()) result.append({select.text(0), select.text(1)});
    return result;
}

void UserStore::movePath(const QString& fromPath, const QString& toPath) {
    const QString from = normalized(fromPath), to = normalized(toPath);
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    // Nothing there (the usual case): one lookup in the index.
    if (!Statement(m_db, "SELECT 1 FROM files WHERE " QCE_FILE_OR_UNDER " LIMIT 1",
                   {from, underLower(from), underUpper(from)}).next())
        return;
    exec("BEGIN");
    // A path taken at the target (a directory moved into another with the
    // same files): the words joined there, then the old paths dropped.
    const bool taken = Statement(m_db, "SELECT 1 FROM files AS moved JOIN files AS target"
                                       " ON target.path = ?4 || substr(moved.path, ?5)"
                                       " WHERE (moved.path = ?1 OR (moved.path >= ?2 AND moved.path < ?3)) LIMIT 1",
                                 {from, underLower(from), underUpper(from), to, afterPrefix(from)}).next();
    if (taken) {
        exec("COMMIT");
        copyPath(from, to);
        removePath(from);
        return;
    }
    // Otherwise only the paths change: the words stay with their file's id.
    Statement(m_db, "UPDATE files SET path = ?4 || substr(path, ?5) WHERE " QCE_FILE_OR_UNDER,
              {from, underLower(from), underUpper(from), to, afterPrefix(from)}).run();
    if (exec("COMMIT")) ++m_ownChanges;
}

void UserStore::copyPath(const QString& fromPath, const QString& toPath) {
    const QString from = normalized(fromPath), to = normalized(toPath);
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    if (!Statement(m_db, "SELECT 1 FROM files WHERE " QCE_FILE_OR_UNDER " LIMIT 1",
                   {from, underLower(from), underUpper(from)}).next())
        return;
    exec("BEGIN");
    Statement(m_db, "INSERT OR IGNORE INTO files(path) SELECT ?4 || substr(path, ?5) FROM files WHERE "
                    QCE_FILE_OR_UNDER,
              {from, underLower(from), underUpper(from), to, afterPrefix(from)}).run();
    Statement(m_db, "INSERT OR IGNORE INTO ignored(file, word) SELECT target.id, ignored.word"
                    " FROM files AS copied JOIN ignored ON ignored.file = copied.id"
                    " JOIN files AS target ON target.path = ?4 || substr(copied.path, ?5)"
                    " WHERE (copied.path = ?1 OR (copied.path >= ?2 AND copied.path < ?3))",
              {from, underLower(from), underUpper(from), to, afterPrefix(from)}).run();
    if (exec("COMMIT")) ++m_ownChanges;
}

void UserStore::removePath(const QString& filePath) {
    const QString path = normalized(filePath);
    if (path.isEmpty()) return;
    if (!Statement(m_db, "SELECT 1 FROM files WHERE " QCE_FILE_OR_UNDER " LIMIT 1",
                   {path, underLower(path), underUpper(path)}).next())
        return;
    exec("BEGIN");
    Statement(m_db, "DELETE FROM ignored WHERE file IN (SELECT id FROM files WHERE " QCE_FILE_OR_UNDER ")",
              {path, underLower(path), underUpper(path)}).run();
    Statement(m_db, "DELETE FROM files WHERE " QCE_FILE_OR_UNDER, {path, underLower(path), underUpper(path)}).run();
    if (exec("COMMIT")) ++m_ownChanges;
}

void UserStore::importOldFiles(const QString& directory) {
    const QDir dir(directory);
    // Only the user dictionaries: the corrections files (<language>.corrections)
    // stay where they are, they hold test pairs rather than the user's words.
    const QStringList files = dir.entryList({QStringLiteral("*.dic")}, QDir::Files);
    if (files.isEmpty() || !m_db) return;
    const QString imported = dir.filePath(QStringLiteral("imported"));
    QDir().mkpath(imported);
    for (const QString& name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString language = QFileInfo(name).completeBaseName();
        exec("BEGIN");
        QTextStream in(&file);
        while (!in.atEnd()) {
            const QString word = in.readLine().trimmed();
            if (!word.isEmpty()) addWord(language, word);
        }
        const bool committed = exec("COMMIT");
        file.close();
        // Kept, out of the way: nothing is read from there again.
        if (!committed) continue;
        QString target = QDir(imported).filePath(name);
        if (QFileInfo::exists(target))
            target += QLatin1Char('.') + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        QFile::rename(file.fileName(), target);
    }
}

} // namespace qce::spell::detail
