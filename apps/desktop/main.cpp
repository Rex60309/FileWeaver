#include "arguments.hpp"
#include "search_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStandardPaths>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("FileWeaver"));
    QApplication::setApplicationName(QStringLiteral("FileWeaver"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/fileweaver.png")));
    QCommandLineParser parser;
    fileweaver::desktop::configureArguments(parser);
    parser.process(application);
    fileweaver::desktop::SearchWindow window;
    if (parser.isSet(QStringLiteral("database"))) {
        window.setDatabase(parser.value(QStringLiteral("database")));
    }
    window.loadPreferences(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                           QStringLiteral("/preferences.json"));
    window.show();
    return QApplication::exec();
}
