#include "local_file.hpp"
#include <QStringList>
#include <windows.h>
namespace fileweaver::desktop {
namespace {
QString localSystemError(const QString &action, unsigned long error) {
    return action + QStringLiteral("（Windows 錯誤 %1）。").arg(error);
}
} // namespace
QString inspectLocalFile(const QString &path, const std::stop_token &stop) {
    const auto root = path.left(3).toStdWString();
    const auto drive = GetDriveTypeW(root.c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_CDROM &&
        drive != DRIVE_RAMDISK) {
        return QStringLiteral("此版本只處理本機磁碟上的檔案，請確認磁碟仍可使用。");
    }
    const auto parts = path.mid(3).split(u'\\');
    QString current = path.left(2);
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (stop.stop_requested()) {
            return QStringLiteral("已取消檔案操作。");
        }
        current += u'\\' + parts[i];
        const auto native = current.toStdWString();
        const auto attributes = GetFileAttributesW(native.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                       ? QStringLiteral("檔案或所在目錄已不存在，請更新索引後重試。")
                       : localSystemError(QStringLiteral("無法檢查檔案，請確認存取權限與磁碟狀態"),
                                          error);
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return QStringLiteral(
                "此版本不處理含有符號連結、junction 或其他 reparse point 的路徑。");
        }
        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (directory != (i + 1 < parts.size())) {
            return QStringLiteral("目標不再是一般檔案，請更新索引後重試。");
        }
        if ((attributes & FILE_ATTRIBUTE_OFFLINE) != 0) {
            return QStringLiteral("檔案目前離線，請先確認它可在本機存取。");
        }
    }
    return {};
}

QString fileLocationPolicyError(const QString &path) {
    if (path.isEmpty() || path.size() > 32760 || QString::fromUtf8(path.toUtf8()) != path ||
        path.contains(QChar{0})) {
        return QStringLiteral("路徑無效或過長，未送出檔案操作請求。");
    }
    if (path.size() < 4 ||
        !((path[0] >= u'A' && path[0] <= u'Z') || (path[0] >= u'a' && path[0] <= u'z')) ||
        path[1] != u':' || path[2] != u'\\') {
        return QStringLiteral("只支援本機磁碟的完整路徑，不支援網址、UNC 或裝置路徑。");
    }
    for (const auto character : path) {
        const auto value = character.unicode();
        if (value < 32 || value == 127 || (value >= 0x202a && value <= 0x202e) ||
            (value >= 0x2066 && value <= 0x2069)) {
            return QStringLiteral("路徑包含控制字元，請複製路徑並手動檢查。");
        }
    }
    const auto parts = path.mid(3).split(u'\\');
    for (const auto &part : parts) {
        if (part.isEmpty() || part == u"." || part == u".." || part.endsWith(u'.') ||
            part.endsWith(u' ') || part.contains(u'/')) {
            return QStringLiteral("路徑格式不受支援，請更新索引或複製路徑手動檢查。");
        }
        for (const auto character : part) {
            if (QStringLiteral("<>\"|?*:").contains(character)) {
                return QStringLiteral("路徑包含不受支援的字元。");
            }
        }
        const auto base = part.section(u'.', 0, 0).toUpper();
        if (base == u"CON" || base == u"PRN" || base == u"AUX" || base == u"NUL" ||
            (base.size() == 4 && (base.startsWith(u"COM") || base.startsWith(u"LPT")) &&
             base[3] >= u'1' && base[3] <= u'9')) {
            return QStringLiteral("不支援 Windows 保留的裝置名稱。");
        }
    }
    return {};
}

} // namespace fileweaver::desktop
