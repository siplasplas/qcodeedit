#pragma once

// Locate real Kate syntax files for tests that exercise KateXmlReader on
// actual definitions. Such tests QSKIP when the file is not found, and never
// download anything themselves (use qce-kate-fetch for that).

#include <qce/kate/KatePaths.h>

#include <QDir>
#include <QFile>
#include <QString>

/// Path of `fileName` (e.g. "xml.xml"), or an empty string if not found.
/// Search order:
///   1. qce::kate::syntaxDir()  (qcodeedit data dir; honours QCE_KATE_DATA_DIR)
///   2. ~/.local/share/org.kde.syntax-highlighting/syntax  (transitional:
///      files installed by KDE's own downloader)
inline QString kateSyntaxPath(const QString& fileName) {
    const QString dirs[] = {
        qce::kate::syntaxDir(),
        QDir::homePath() + QStringLiteral("/.local/share/org.kde.syntax-highlighting/syntax"),
    };
    for (const QString& dir : dirs) {
        if (dir.isEmpty()) continue;
        const QString p = dir + QLatin1Char('/') + fileName;
        if (QFile::exists(p)) return p;
    }
    return {};
}
