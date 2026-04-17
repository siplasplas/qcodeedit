#include "DemoWindow.h"

#include <QApplication>
#include <QCommandLineParser>

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

    DemoWindow window;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        window.loadFile(args.first());
    }
    window.show();

    return app.exec();
}
