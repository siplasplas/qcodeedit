#pragma once
// The user's own spelling data, shared by all programs, in one SQLite
// database (userDataPath()): the words of the user dictionary and the own
// corrections of each language, and the words ignored in each file (by its
// path). Each process has one connection; changes of other processes show
// in stamp().

#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

struct sqlite3;

namespace qce::spell::detail {

class UserStore {
public:
    /// The process's store (opened the first time; does nothing without a
    /// data location).
    static UserStore& instance();
    ~UserStore();

    /// Changes whenever this or another program changed the data.
    qint64 stamp();

    QSet<QString> words(const QString& language);
    void addWord(const QString& language, const QString& word);
    void removeWord(const QString& language, const QString& word);

    QHash<QString, QString> corrections(const QString& language);
    void addCorrection(const QString& language, const QString& typed, const QString& correction);
    void removeCorrection(const QString& language, const QString& typed);

    QSet<QString> ignored(const QString& path);
    void addIgnored(const QString& path, const QString& word);
    void removeIgnored(const QString& path, const QString& word);
    /// Every file with ignored words and the words, for the user dictionary window.
    QList<QPair<QString, QString>> allIgnored();

    /// A file or directory moved, copied or deleted: the ignored words of the
    /// file, or of the files under the directory, follow.
    void movePath(const QString& from, const QString& to);
    void copyPath(const QString& from, const QString& to);
    void removePath(const QString& path);

private:
    UserStore();
    bool exec(const char* sql);
    /// The tables of kSchemaVersion (PRAGMA user_version); a database of 1.46
    /// or 1.47 is converted.
    void createSchema();
    /// The user dictionaries (<language>.dic) of earlier versions, taken in
    /// once and moved to an "imported" directory beside the database.
    void importOldFiles(const QString& directory);

    sqlite3* m_db = nullptr;
    qint64 m_ownChanges = 0;
};

} // namespace qce::spell::detail
