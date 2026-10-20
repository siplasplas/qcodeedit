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

// The path of a directory's files: "dir/" as a LIKE pattern (with % and _
// of the name escaped).
QString underPattern(const QString& directory) {
    QString escaped = directory;
    escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
        .replace(QLatin1Char('%'), QLatin1String("\\%"))
        .replace(QLatin1Char('_'), QLatin1String("\\_"));
    return escaped + QLatin1String("/%");
}

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
    exec("CREATE TABLE IF NOT EXISTS words(language TEXT NOT NULL, word TEXT NOT NULL,"
         " PRIMARY KEY(language, word))");
    exec("CREATE TABLE IF NOT EXISTS corrections(language TEXT NOT NULL, typed TEXT NOT NULL,"
         " correction TEXT NOT NULL, PRIMARY KEY(language, typed))");
    exec("CREATE TABLE IF NOT EXISTS ignored(path TEXT NOT NULL, word TEXT NOT NULL, PRIMARY KEY(path, word))");
    importOldFiles(QFileInfo(path).absolutePath());
}

UserStore::~UserStore() {
    if (m_db) sqlite3_close(m_db);
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
    Statement select(m_db, "SELECT word FROM ignored WHERE path = ?", {path});
    while (select.next()) result.insert(select.text(0));
    return result;
}

void UserStore::addIgnored(const QString& path, const QString& word) {
    if (Statement(m_db, "INSERT OR IGNORE INTO ignored(path, word) VALUES(?, ?)", {path, word}).run())
        ++m_ownChanges;
}

void UserStore::removeIgnored(const QString& path, const QString& word) {
    if (Statement(m_db, "DELETE FROM ignored WHERE path = ? AND word = ?", {path, word}).run()) ++m_ownChanges;
}

QList<QPair<QString, QString>> UserStore::allIgnored() {
    QList<QPair<QString, QString>> result;
    Statement select(m_db, "SELECT path, word FROM ignored ORDER BY path, word", {});
    while (select.next()) result.append({select.text(0), select.text(1)});
    return result;
}

void UserStore::movePath(const QString& from, const QString& to) {
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    exec("BEGIN");
    // A file, and the files under a directory (their paths keep the rest).
    Statement(m_db, "UPDATE OR REPLACE ignored SET path = ? WHERE path = ?", {to, from}).run();
    Statement(m_db, "UPDATE OR REPLACE ignored SET path = ? || substr(path, ?) WHERE path LIKE ? ESCAPE '\\'",
              {to, QString::number(from.size() + 1), underPattern(from)}).run();
    exec("COMMIT");
    ++m_ownChanges;
}

void UserStore::copyPath(const QString& from, const QString& to) {
    if (from.isEmpty() || to.isEmpty() || from == to) return;
    exec("BEGIN");
    Statement(m_db, "INSERT OR IGNORE INTO ignored(path, word) SELECT ?, word FROM ignored WHERE path = ?",
              {to, from}).run();
    Statement(m_db, "INSERT OR IGNORE INTO ignored(path, word) SELECT ? || substr(path, ?), word FROM ignored"
                    " WHERE path LIKE ? ESCAPE '\\'",
              {to, QString::number(from.size() + 1), underPattern(from)}).run();
    exec("COMMIT");
    ++m_ownChanges;
}

void UserStore::removePath(const QString& path) {
    if (path.isEmpty()) return;
    Statement(m_db, "DELETE FROM ignored WHERE path = ? OR path LIKE ? ESCAPE '\\'", {path, underPattern(path)}).run();
    ++m_ownChanges;
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
