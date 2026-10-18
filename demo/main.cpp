#include "DemoWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFontDatabase>
#include <qce/kate/KatePaths.h>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qcodeedit-demo"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::tr("qcodeedit demo: minimal file viewer (read-only)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("file"),
        QApplication::tr("Optional file to open on startup."));
    parser.process(app);

    // Kate data kept up to qcodeedit 1.12 in ~/.local/share/qcodeedit moves to
    // ~/.local/share/kate-data, once, before anything reads it.
    const auto moved = qce::kate::migrateDataRoot();
    for (const QString& line : moved.log) qInfo("%s", qPrintable(line));
    for (const QString& line : moved.warnings) qWarning("%s", qPrintable(line));

    DemoWindow window;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.loadFile(args.first());
    }
    window.show();

    return app.exec();
}
