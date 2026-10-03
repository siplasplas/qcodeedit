#include <qce/kate/KatePaths.h>
#include <qce/kate/KateSyntaxIndex.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <optional>

namespace {
thread_local QStringList* activeDiagnostics = nullptr;
QtMessageHandler previousHandler = nullptr;

void collectDiagnostics(QtMsgType type, const QMessageLogContext& context,
                        const QString& message) {
    if (activeDiagnostics && (type == QtWarningMsg || type == QtCriticalMsg)) {
        activeDiagnostics->append(message);
    } else if (previousHandler) {
        previousHandler(type, context, message);
    } else {
        QTextStream(stderr) << qFormatLogMessage(type, context, message) << Qt::endl;
    }
}

class DiagnosticCapture {
public:
    explicit DiagnosticCapture(QStringList& messages) {
        activeDiagnostics = &messages;
        previousHandler = qInstallMessageHandler(collectDiagnostics);
    }
    ~DiagnosticCapture() {
        qInstallMessageHandler(previousHandler);
        activeDiagnostics = nullptr;
    }
};
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("qce-kate-check"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Load every Kate syntax XML and report failures and diagnostics. "
        "An OK result requires a highlighter with no warnings."));
    parser.addHelpOption();
    const QCommandLineOption syntaxOption(
        QStringLiteral("syntax-dir"), QStringLiteral("Directory containing *.xml."),
        QStringLiteral("directory"));
    parser.addOption(syntaxOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString directory = parser.isSet(syntaxOption)
        ? parser.value(syntaxOption) : qce::kate::syntaxDir();
    if (directory.isEmpty() || !QFileInfo(directory).isDir()) {
        err << "Syntax directory does not exist: " << directory << Qt::endl;
        return 2;
    }
    const QDir dir(directory);
    out << "Syntax directory: " << dir.absolutePath() << Qt::endl;
    const auto files = dir.entryInfoList({QStringLiteral("*.xml")}, QDir::Files,
                                         QDir::Name);
    if (files.isEmpty()) {
        err << "No XML definitions found; no test was performed." << Qt::endl;
        return 2;
    }

    // Use the component's shared index for its normal data layout. For an
    // arbitrary directory the reader resolves includes directly from that dir.
    std::optional<qce::kate::KateSyntaxIndex> index;
    if (dir.absolutePath() == QDir(qce::kate::syntaxDir()).absolutePath()) {
        index = qce::kate::KateSyntaxIndex::load(qce::kate::dataDir());
    }
    QElapsedTimer timer;
    timer.start();
    int attempted = 0;
    int succeeded = 0;
    int noHighlighter = 0;
    int withDiagnostics = 0;
    QStringList failedFiles;
    for (const auto& file : files) {
        ++attempted;
        out << '[' << attempted << '/' << files.size() << "] "
            << file.fileName() << " ... " << Qt::flush;
        QStringList diagnostics;
        std::unique_ptr<qce::RulesHighlighter> highlighter;
        {
            DiagnosticCapture capture(diagnostics);
            highlighter = index
                ? KateXmlReader::load(file.absoluteFilePath(), KateTheme{}, *index)
                : KateXmlReader::load(file.absoluteFilePath());
        }
        if (highlighter && diagnostics.isEmpty()) {
            ++succeeded;
            out << "OK" << Qt::endl;
        } else {
            failedFiles.append(file.fileName());
            if (!highlighter) {
                ++noHighlighter;
                out << "FAIL (no highlighter)" << Qt::endl;
            } else {
                ++withDiagnostics;
                out << "FAIL (loaded with diagnostics)" << Qt::endl;
            }
            for (const auto& message : diagnostics) {
                out << "    " << message << Qt::endl;
            }
        }
    }
    out << "\nSummary\n"
        << "Attempted: " << attempted << '\n'
        << "Succeeded (no diagnostics): " << succeeded << '\n'
        << "Failed: " << failedFiles.size() << '\n'
        << "  No highlighter: " << noHighlighter << '\n'
        << "  Loaded with diagnostics: " << withDiagnostics << '\n'
        << "Elapsed: " << timer.elapsed() << " ms" << Qt::endl;
    if (!failedFiles.isEmpty()) {
        out << "Failed files:\n" << failedFiles.join(QLatin1Char('\n')) << Qt::endl;
    }
    return failedFiles.isEmpty() ? 0 : 1;
}
