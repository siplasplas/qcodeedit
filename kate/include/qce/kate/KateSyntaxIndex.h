#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace qce::kate {

/// Header data of one Kate syntax definition (attributes of <language>),
/// plus the file stamp used to detect changes.
struct LanguageEntry {
    QString file;          ///< file name inside syntaxDir, e.g. "cpp.xml"
    QString name;          ///< language name, e.g. "C++" (used by ##C++ includes)
    QString section;
    QString extensions;    ///< raw ';'-separated wildcard list
    QString mimetype;
    QString kateversion;   ///< raw, e.g. "5.62"
    int     version     = 0;
    int     priority    = 0;
    bool    hidden      = false;
    bool    unsupported = false;  ///< kateversion > supportedSyntaxVersion()
    qint64  size        = 0;
    qint64  mtime       = 0;      ///< msecs since epoch

    friend bool operator==(const LanguageEntry&, const LanguageEntry&) = default;
};

/// Persistent index of a Kate syntax directory, stored as
/// <dataDir>/index.json and rewritten as a whole on save().
///
/// Lets consumers list languages, pick a definition by file name and resolve
/// cross-language ##Name includes without parsing every XML file. On load()
/// only files whose size or mtime differ from the stored entry are re-read
/// (and only up to their <language> element).
///
/// The index never touches the network.
class KateSyntaxIndex {
public:
    /// Format version written to index.json ("format").
    static constexpr int kFormat = 1;

    KateSyntaxIndex() = default;

    /// Read <dataDir>/index.json (if present and matching kFormat and
    /// supportedSyntaxVersion()), then refresh() against <dataDir>/syntax.
    /// Does not write; call saveIfDirty() to persist changes.
    static KateSyntaxIndex load(const QString& dataDir);

    /// Re-stat <dataDir>/syntax/*.xml: re-read changed or new files, drop
    /// entries for removed files. Returns true if anything changed.
    bool refresh();

    /// Force `file` to be re-read on the next refresh() even if its size and
    /// mtime look unchanged. Use after rewriting a file in place: filesystem
    /// timestamps are coarse (a few ms), so a same-size rewrite can be missed.
    void invalidate(const QString& file);

    /// Write index.json atomically (creates dataDir if needed).
    bool save();
    bool saveIfDirty() { return m_dirty ? save() : true; }
    bool isDirty() const { return m_dirty; }

    QString dataDir()   const { return m_dataDir; }
    QString syntaxDir() const;
    QString indexPath() const;

    /// All entries, sorted by file name.
    const QList<LanguageEntry>& languages() const { return m_entries; }

    /// Lookup by language name (case-sensitive, like Kate). If several files
    /// declare the same name, the one with the highest version wins.
    const LanguageEntry* byName(const QString& name) const;
    const LanguageEntry* byFile(const QString& file) const;

    /// Absolute path of the XML file for `e`.
    QString filePath(const LanguageEntry& e) const;

    /// Definitions whose extensions match the file name of `path`, sorted by
    /// priority (highest first), then by name. Unsupported definitions are
    /// skipped unless `includeUnsupported`.
    QList<const LanguageEntry*> forFileName(const QString& path,
                                            bool includeUnsupported = false) const;

    /// Read the <language> header of a Kate XML file. Fills everything except
    /// size/mtime. Returns nullopt if the file can't be read or has no
    /// <language name="..."> element.
    static std::optional<LanguageEntry> readHeader(const QString& filePath);

private:
    void rebuildLookup();

    QString                m_dataDir;
    QList<LanguageEntry>   m_entries;
    QHash<QString, int>    m_byName;
    QHash<QString, int>    m_byFile;
    bool                   m_dirty = false;
};

/// Kate-style wildcard match of a whole file name: '*' matches any run of
/// characters (including none), '?' exactly one; everything else literally,
/// case-sensitive.
bool wildcardMatch(QStringView candidate, QStringView pattern);

} // namespace qce::kate
