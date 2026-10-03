// qce-kate-fetch — download Kate syntax definitions and themes into the
// qcodeedit data directory (or --dir), fetching only what is missing.

#include <qce/kate/KateDataDownloader.h>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxVersion.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qce-kate-fetch"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Download Kate syntax definitions %1 and themes.")
            .arg(qce::kate::supportedSyntaxVersion().toString()));
    parser.addHelpOption();
    const QCommandLineOption dirOpt(QStringLiteral("dir"),
        QStringLiteral("Target data directory (default: %1).").arg(qce::kate::dataDir()),
        QStringLiteral("path"));
    const QCommandLineOption checkOpt(QStringLiteral("check"),
        QStringLiteral("Only report whether a download is needed (exit 0 = up to date, 1 = needed)."));
    parser.addOption(dirOpt);
    parser.addOption(checkOpt);
    parser.process(app);

    QTextStream out(stdout);
    qce::kate::KateDataDownloader dl;
    if (parser.isSet(dirOpt)) dl.setDataDir(parser.value(dirOpt));
    out << "data dir: " << dl.dataDir() << Qt::endl;

    const bool needed = dl.mustDownload();
    if (parser.isSet(checkOpt)) {
        out << (needed ? "download needed" : "up to date") << Qt::endl;
        return needed ? 1 : 0;
    }

    QObject::connect(&dl, &qce::kate::KateDataDownloader::progress,
                     [&](int done, int total) {
                         if (total > 0) out << "\r" << done << "/" << total << Qt::flush;
                     });
    QObject::connect(&dl, &qce::kate::KateDataDownloader::finished,
                     [&](bool ok, int downloaded, int failed) {
                         out << "\ndownloaded " << downloaded << ", failed " << failed
                             << (ok ? "" : " (incomplete)") << Qt::endl;
                         app.exit(ok ? 0 : 2);
                     });
    if (!dl.start()) return 2;
    return app.exec();
}
