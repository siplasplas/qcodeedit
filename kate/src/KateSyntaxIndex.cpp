#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateSyntaxVersion.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QXmlStreamReader>

#include <algorithm>

namespace qce::kate {

// -----------------------------------------------------------------------------
// Wildcards
// -----------------------------------------------------------------------------

bool wildcardMatch(QStringView candidate, QStringView pattern) {
    // Iterative glob with single-star backtracking: O(n*m) worst case.
    qsizetype c = 0, p = 0;
    qsizetype starP = -1, starC = 0;
    while (c < candidate.size()) {
        if (p < pattern.size()
            && (pattern[p] == QLatin1Char('?') || pattern[p] == candidate[c])) {
            ++c;
            ++p;
        } else if (p < pattern.size() && pattern[p] == QLatin1Char('*')) {
            starP = p++;
            starC = c;
        } else if (starP >= 0) {
            p = starP + 1;
            c = ++starC;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == QLatin1Char('*')) ++p;
    return p == pattern.size();
}

// -----------------------------------------------------------------------------
// Header parsing
// -----------------------------------------------------------------------------

static bool parseBool(QStringView v) {
    return v == QLatin1String("true") || v == QLatin1String("1");
}

struct KateSyntaxIndex::Private {
    QString                m_dataDir;
    QList<LanguageEntry>   m_entries;
    QHash<QString, int>    m_byName;
    QHash<QString, int>    m_byFile;
    bool                   m_dirty = false;
};

KateSyntaxIndex::KateSyntaxIndex() : d(std::make_unique<Private>()) {}


std::optional<LanguageEntry> KateSyntaxIndex::readHeader(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;

    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        if (xml.name() != QLatin1String("language")) return std::nullopt;

        const QXmlStreamAttributes a = xml.attributes();
        LanguageEntry e;
        e.file        = QFileInfo(filePath).fileName();
        e.name        = a.value(QLatin1String("name")).toString();
        e.section     = a.value(QLatin1String("section")).toString();
        e.extensions  = a.value(QLatin1String("extensions")).toString();
        e.mimetype    = a.value(QLatin1String("mimetype")).toString();
        e.kateversion = a.value(QLatin1String("kateversion")).toString();
        e.version     = a.value(QLatin1String("version")).toInt();
        e.priority    = a.value(QLatin1String("priority")).toInt();
        e.hidden      = parseBool(a.value(QLatin1String("hidden")));
        if (const auto kv = SyntaxVersion::parse(e.kateversion))
            e.unsupported = *kv > supportedSyntaxVersion();
        if (e.name.isEmpty()) return std::nullopt;
        return e;
    }
    return std::nullopt;
}

// -----------------------------------------------------------------------------
// JSON
// -----------------------------------------------------------------------------

static QJsonObject toJson(const LanguageEntry& e) {
    QJsonObject o;
    o[QLatin1String("file")]        = e.file;
    o[QLatin1String("name")]        = e.name;
    o[QLatin1String("section")]     = e.section;
    o[QLatin1String("extensions")]  = e.extensions;
    o[QLatin1String("mimetype")]    = e.mimetype;
    o[QLatin1String("kateversion")] = e.kateversion;
    o[QLatin1String("version")]     = e.version;
    o[QLatin1String("priority")]    = e.priority;
    o[QLatin1String("hidden")]      = e.hidden;
    o[QLatin1String("unsupported")] = e.unsupported;
    o[QLatin1String("size")]        = e.size;
    o[QLatin1String("mtime")]       = e.mtime;
    return o;
}

static LanguageEntry fromJson(const QJsonObject& o) {
    LanguageEntry e;
    e.file        = o.value(QLatin1String("file")).toString();
    e.name        = o.value(QLatin1String("name")).toString();
    e.section     = o.value(QLatin1String("section")).toString();
    e.extensions  = o.value(QLatin1String("extensions")).toString();
    e.mimetype    = o.value(QLatin1String("mimetype")).toString();
    e.kateversion = o.value(QLatin1String("kateversion")).toString();
    e.version     = o.value(QLatin1String("version")).toInt();
    e.priority    = o.value(QLatin1String("priority")).toInt();
    e.hidden      = o.value(QLatin1String("hidden")).toBool();
    e.unsupported = o.value(QLatin1String("unsupported")).toBool();
    e.size        = o.value(QLatin1String("size")).toInteger();
    e.mtime       = o.value(QLatin1String("mtime")).toInteger();
    return e;
}

// -----------------------------------------------------------------------------
// KateSyntaxIndex
// -----------------------------------------------------------------------------

QString KateSyntaxIndex::syntaxDir() const {
    return d->m_dataDir + QStringLiteral("/syntax");
}

QString KateSyntaxIndex::indexPath() const {
    return d->m_dataDir + QStringLiteral("/index.json");
}

QString KateSyntaxIndex::filePath(const LanguageEntry& e) const {
    return syntaxDir() + QLatin1Char('/') + e.file;
}

KateSyntaxIndex KateSyntaxIndex::load(const QString& dataDir) {
    KateSyntaxIndex idx;
    idx.d->m_dataDir = QDir::cleanPath(dataDir);

    QFile f(idx.indexPath());
    bool loaded = false;
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (root.value(QLatin1String("format")).toInt() == kFormat
            && root.value(QLatin1String("syntaxVersion")).toString()
                   == supportedSyntaxVersion().toString()) {
            const QJsonArray langs = root.value(QLatin1String("languages")).toArray();
            idx.d->m_entries.reserve(langs.size());
            for (const QJsonValue& v : langs) {
                LanguageEntry e = fromJson(v.toObject());
                if (!e.file.isEmpty() && !e.name.isEmpty())
                    idx.d->m_entries.append(std::move(e));
            }
            loaded = true;
        }
    }

    // Missing, unreadable or stale index.json → must be (re)written.
    idx.d->m_dirty = !loaded;
    idx.refresh();
    return idx;
}

bool KateSyntaxIndex::refresh() {
    QHash<QString, LanguageEntry> old;
    for (LanguageEntry& e : d->m_entries) old.insert(e.file, std::move(e));

    QList<LanguageEntry> fresh;
    bool changed = false;
    const QFileInfoList files =
        QDir(syntaxDir()).entryInfoList({QStringLiteral("*.xml")}, QDir::Files, QDir::Name);
    fresh.reserve(files.size());
    for (const QFileInfo& fi : files) {
        const qint64 size  = fi.size();
        const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();

        auto it = old.find(fi.fileName());
        if (it != old.end() && it->size == size && it->mtime == mtime) {
            fresh.append(std::move(*it));
            old.erase(it);
            continue;
        }
        if (it != old.end()) old.erase(it);

        changed = true;
        if (auto e = readHeader(fi.filePath())) {
            e->size  = size;
            e->mtime = mtime;
            fresh.append(std::move(*e));
        }
    }
    if (!old.isEmpty()) changed = true;   // files removed from disk

    d->m_entries = std::move(fresh);
    rebuildLookup();
    if (changed) d->m_dirty = true;
    return changed;
}

void KateSyntaxIndex::invalidate(const QString& file) {
    const auto it = d->m_byFile.constFind(file);
    if (it == d->m_byFile.cend()) return;
    d->m_entries[*it].size  = -1;   // never matches a real file
    d->m_entries[*it].mtime = -1;
}

bool KateSyntaxIndex::save() {
    if (d->m_dataDir.isEmpty()) return false;
    if (!QDir().mkpath(d->m_dataDir)) return false;

    QJsonArray langs;
    for (const LanguageEntry& e : d->m_entries) langs.append(toJson(e));
    QJsonObject root;
    root[QLatin1String("format")]        = kFormat;
    root[QLatin1String("syntaxVersion")] = supportedSyntaxVersion().toString();
    root[QLatin1String("languages")]     = langs;

    QSaveFile f(indexPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) return false;
    d->m_dirty = false;
    return true;
}

void KateSyntaxIndex::rebuildLookup() {
    d->m_byName.clear();
    d->m_byFile.clear();
    for (int i = 0; i < d->m_entries.size(); ++i) {
        const LanguageEntry& e = d->m_entries[i];
        d->m_byFile.insert(e.file, i);
        auto it = d->m_byName.find(e.name);
        if (it == d->m_byName.end() || d->m_entries[*it].version < e.version)
            d->m_byName.insert(e.name, i);
    }
}

const LanguageEntry* KateSyntaxIndex::byName(const QString& name) const {
    const auto it = d->m_byName.constFind(name);
    return it == d->m_byName.cend() ? nullptr : &d->m_entries[*it];
}

const LanguageEntry* KateSyntaxIndex::byFile(const QString& file) const {
    const auto it = d->m_byFile.constFind(file);
    return it == d->m_byFile.cend() ? nullptr : &d->m_entries[*it];
}

QList<const LanguageEntry*> KateSyntaxIndex::forFileName(const QString& path,
                                                         bool includeUnsupported) const {
    const QString fileName = QFileInfo(path).fileName();
    QList<const LanguageEntry*> result;
    if (fileName.isEmpty()) return result;

    for (const LanguageEntry& e : d->m_entries) {
        if (e.unsupported && !includeUnsupported) continue;
        const auto patterns = QStringView(e.extensions).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (QStringView pat : patterns) {
            pat = pat.trimmed();
            if (!pat.isEmpty() && wildcardMatch(fileName, pat)) {
                result.append(&e);
                break;
            }
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const LanguageEntry* a, const LanguageEntry* b) {
                         if (a->priority != b->priority) return a->priority > b->priority;
                         return a->name < b->name;
                     });
    return result;
}


KateSyntaxIndex::~KateSyntaxIndex() = default;
KateSyntaxIndex::KateSyntaxIndex(const KateSyntaxIndex& other) : d(std::make_unique<Private>(*other.d)) {}
KateSyntaxIndex& KateSyntaxIndex::operator=(const KateSyntaxIndex& other) { if (this != &other) *d = *other.d; return *this; }
// The source stays usable, with an empty state.
KateSyntaxIndex::KateSyntaxIndex(KateSyntaxIndex&& other) noexcept : d(std::make_unique<Private>()) { d.swap(other.d); }
KateSyntaxIndex& KateSyntaxIndex::operator=(KateSyntaxIndex&& other) noexcept { d.swap(other.d); return *this; }
bool KateSyntaxIndex::saveIfDirty() { return d->m_dirty ? save() : true; }
bool KateSyntaxIndex::isDirty() const { return d->m_dirty; }
QString KateSyntaxIndex::dataDir() const { return d->m_dataDir; }
const QList<LanguageEntry>& KateSyntaxIndex::languages() const { return d->m_entries; }

} // namespace qce::kate
