#include <qce/kate/KateTheme.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

static KateTheme::StyleEntry parseStyleEntry(const QJsonObject& obj) {
    KateTheme::StyleEntry e;
    const QString fg = obj.value(QLatin1String("text-color")).toString();
    if (!fg.isEmpty()) e.fg = QColor(fg);
    const QString bg = obj.value(QLatin1String("background-color")).toString();
    if (!bg.isEmpty()) e.bg = QColor(bg);
    e.bold      = obj.value(QLatin1String("bold")).toBool(false);
    e.italic    = obj.value(QLatin1String("italic")).toBool(false);
    e.underline = obj.value(QLatin1String("underline")).toBool(false);
    return e;
}

KateTheme KateTheme::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull()) return {};

    const QJsonObject root = doc.object();

    KateTheme theme;
    theme.name = root.value(QLatin1String("metadata"))
                     .toObject()
                     .value(QLatin1String("name"))
                     .toString();
    if (theme.name.isEmpty()) return {};

    const QJsonObject editorColors =
        root.value(QLatin1String("editor-colors")).toObject();
    const QString bg = editorColors.value(QLatin1String("BackgroundColor")).toString();
    if (!bg.isEmpty()) theme.editorBackground = QColor(bg);

    const QJsonObject textStyles =
        root.value(QLatin1String("text-styles")).toObject();
    for (auto it = textStyles.begin(); it != textStyles.end(); ++it)
        theme.styles.insert(it.key(), parseStyleEntry(it.value().toObject()));

    return theme;
}

QList<QPair<QString, QString>> KateTheme::listThemes(const QString& dir) {
    QList<QPair<QString, QString>> result;
    const QStringList entries =
        QDir(dir).entryList({QStringLiteral("*.theme")}, QDir::Files);
    for (const QString& entry : entries) {
        const QString path = dir + QLatin1Char('/') + entry;
        const KateTheme t = KateTheme::load(path);
        if (t.isValid())
            result.append({t.name, path});
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}
