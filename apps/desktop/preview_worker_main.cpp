#include "preview.hpp"
#include <QCoreApplication>
#include <cstdio>
#include <fcntl.h>
#include <io.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setLibraryPaths({QCoreApplication::applicationDirPath()});
    const auto arguments = app.arguments();
    if (arguments.size() != 3) {
        return 2;
    }
    const auto payload = fileweaver::desktop::previewDocument({arguments[1], arguments[2]});
    _setmode(_fileno(stdout), _O_BINARY);
    return std::fwrite(payload.constData(), 1, static_cast<std::size_t>(payload.size()), stdout) ==
                   static_cast<std::size_t>(payload.size())
               ? 0
               : 1;
}
