#include "arguments.hpp"
#include <QCommandLineParser>

namespace fileweaver::desktop {
void configureArguments(QCommandLineParser &parser) {
    parser.setApplicationDescription(QStringLiteral("FileWeaver Windows 檔名搜尋視窗"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("database")},
                                        QStringLiteral("開啟既有索引；不會建立或重掃來源。"),
                                        QStringLiteral("path")));
}
} // namespace fileweaver::desktop
