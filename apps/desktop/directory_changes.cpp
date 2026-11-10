#include "directory_changes.hpp"
#include "file_open.hpp"
#include <QDir>
#include <QString>
#include <array>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <windows.h>

namespace fileweaver::desktop {
namespace {
struct CloseHandleDeleter {
    void operator()(HANDLE handle) const {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, CloseHandleDeleter>;
[[noreturn]] void fail(const char *action) {
    throw std::runtime_error(std::string(action) + " (Windows error " +
                             std::to_string(GetLastError()) + ")");
}
QString finalPath(HANDLE handle) {
    const auto needed = GetFinalPathNameByHandleW(handle, nullptr, 0, VOLUME_NAME_GUID);
    if (needed == 0) {
        fail("Cannot resolve final watch storage path");
    }
    if (needed > 32768) {
        throw std::runtime_error("Watch storage path exceeds safety limit");
    }
    std::wstring buffer(static_cast<std::size_t>(needed) + 1, L'\0');
    const auto written = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()), VOLUME_NAME_GUID);
    if (written == 0) {
        fail("Cannot resolve final watch storage path");
    }
    if (written >= buffer.size()) {
        throw std::runtime_error("Watch storage path changed during validation");
    }
    buffer.resize(written);
    return QString::fromStdWString(buffer);
}
void validate(const WatchTarget &target) {
    const auto root =
        std::filesystem::path(std::u8string(target.root.path.begin(), target.root.path.end()));
    const auto path = QDir::toNativeSeparators(QString::fromStdWString(root.native()));
    const auto probe = path + (path.endsWith(u'\\') ? QString{} : QStringLiteral("\\")) +
                       QStringLiteral("__watch_probe__");
    if (!fileLocationPolicyError(probe).isEmpty()) {
        throw std::runtime_error("Watch requires an unambiguous local drive root");
    }
    const auto drive = GetDriveTypeW(path.left(3).toStdWString().c_str());
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_CDROM &&
        drive != DRIVE_RAMDISK) {
        throw std::runtime_error("Watch requires an available local drive");
    }
    QString current = path.left(3);
    const auto inspect = [](const QString &part) {
        const auto attrs = GetFileAttributesW(part.toStdWString().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            fail("Cannot inspect watch root");
        }
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attrs & (static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT) |
                      static_cast<DWORD>(FILE_ATTRIBUTE_OFFLINE))) != 0) {
            throw std::runtime_error(
                "Watch root/ancestors must be available directories without reparse points");
        }
    };
    inspect(current);
    for (const auto &part : path.mid(3).split(u'\\', Qt::SkipEmptyParts)) {
        if (!current.endsWith(u'\\')) {
            current += u'\\';
        }
        current += part;
        inspect(current);
    }
    std::error_code error;
    const auto database = std::filesystem::canonical(target.database, error);
    if (error) {
        throw std::runtime_error("Cannot resolve watch database");
    }
    const auto relative = database.lexically_relative(root);
    const auto original = QDir::toNativeSeparators(QString::fromStdWString(
        std::filesystem::absolute(target.database).lexically_normal().native()));
    // Windows path comparisons are case-insensitive; compare folded components too.
    const auto db_text = QDir::toNativeSeparators(QString::fromStdWString(database.native()));
    if (!fileLocationPolicyError(db_text).isEmpty()) {
        throw std::runtime_error("Watch database must use a local, unambiguous file path");
    }
    const auto db_drive = GetDriveTypeW(db_text.left(3).toStdWString().c_str());
    if (db_drive != DRIVE_FIXED && db_drive != DRIVE_REMOVABLE && db_drive != DRIVE_CDROM &&
        db_drive != DRIVE_RAMDISK) {
        throw std::runtime_error("Watch database must be on an available local drive");
    }
    const auto prefix = path.endsWith(u'\\') ? path : path + u'\\';
    if ((!relative.empty() && *relative.begin() != "..") ||
        (db_text.startsWith(prefix, Qt::CaseInsensitive) ||
         original.startsWith(prefix, Qt::CaseInsensitive))) {
        throw std::runtime_error(
            "Initialize/index a database outside the watched root to prevent notification loops");
    }
    const Handle root_handle(
        CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (root_handle.get() == INVALID_HANDLE_VALUE) {
        fail("Cannot inspect final watch root");
    }
    const auto final_root = finalPath(root_handle.get());
    const auto final_prefix = final_root.endsWith(u'\\') ? final_root : final_root + u'\\';
    for (const auto &suffix : {L"", L"-wal", L"-shm", L"-journal"}) {
        const auto file = database.native() + suffix;
        const Handle handle(CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                                        nullptr));
        if (handle.get() == INVALID_HANDLE_VALUE) {
            const auto error_code = GetLastError();
            if (*suffix != L'\0' &&
                (error_code == ERROR_FILE_NOT_FOUND || error_code == ERROR_PATH_NOT_FOUND)) {
                continue;
            }
            fail("Cannot inspect watch database identity");
        }
        if (finalPath(handle.get()).startsWith(final_prefix, Qt::CaseInsensitive)) {
            throw std::runtime_error(
                "Watch index database/sidecars must be physically outside the watched root");
        }
        BY_HANDLE_FILE_INFORMATION identity{};
        if (GetFileInformationByHandle(handle.get(), &identity) == FALSE) {
            fail("Cannot inspect watch database identity");
        }
        if (identity.nNumberOfLinks > 1 ||
            (identity.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error(
                "Watching with hard-linked/reparse database or sidecars is not supported");
        }
    }
}
class WindowsChanges final : public DirectoryChanges {
  public:
    explicit WindowsChanges(const WatchTarget &target) {
        validate(target);
        const auto root =
            std::filesystem::path(std::u8string(target.root.path.begin(), target.root.path.end()));
        directory_.reset(CreateFileW(
            root.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr));
        if (directory_.get() == INVALID_HANDLE_VALUE) {
            fail("Cannot open watch root");
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (GetFileInformationByHandle(directory_.get(), &info) == FALSE) {
            fail("Cannot inspect opened watch root");
        }
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            throw std::runtime_error("Watch root became a reparse point");
        }
        event_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        cancel_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event_ || !cancel_) {
            fail("Cannot create watch event");
        }
        overlapped_.hEvent = event_.get();
        arm();
    }
    ~WindowsChanges() override {
        if (pending_) {
            CancelIoEx(directory_.get(), &overlapped_);
            DWORD bytes = 0;
            // Buffer/OVERLAPPED must remain alive until the driver completes cancellation.
            GetOverlappedResult(directory_.get(), &overlapped_, &bytes, TRUE);
        }
    }
    DirectoryChange poll(const std::stop_token &stop) override {
        const std::stop_callback callback(stop, [this] { SetEvent(cancel_.get()); });
        const std::array<HANDLE, 2> handles{cancel_.get(), event_.get()};
        const auto result =
            WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, 200);
        if (result == WAIT_OBJECT_0 || stop.stop_requested()) {
            return DirectoryChange::cancelled;
        }
        if (result == WAIT_TIMEOUT) {
            return DirectoryChange::timeout;
        }
        if (result != WAIT_OBJECT_0 + 1) {
            fail("Cannot wait for directory changes");
        }
        DWORD bytes = 0;
        const auto completed = GetOverlappedResult(directory_.get(), &overlapped_, &bytes, FALSE);
        const auto error = completed != FALSE ? ERROR_SUCCESS : GetLastError();
        if (error != ERROR_IO_INCOMPLETE) {
            pending_ = false;
        }
        if (error != ERROR_SUCCESS && error != ERROR_NOTIFY_ENUM_DIR) {
            SetLastError(error);
            fail("Directory notification interrupted");
        }
        arm(); // Keep one outstanding notification while reconciliation scans run.
        return bytes == 0 || error == ERROR_NOTIFY_ENUM_DIR ? DirectoryChange::overflow
                                                            : DirectoryChange::changed;
    }

  private:
    void arm() {
        ResetEvent(event_.get());
        if (ReadDirectoryChangesW(directory_.get(), buffer_.data(),
                                  static_cast<DWORD>(sizeof(buffer_)), TRUE,
                                  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                      FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                      FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_CREATION,
                                  nullptr, &overlapped_, nullptr) == FALSE) {
            fail("Cannot arm directory notifications");
        }
        pending_ = true;
    }
    Handle directory_;
    Handle event_;
    Handle cancel_;
    OVERLAPPED overlapped_{};
    std::array<DWORD, 8192> buffer_{};
    bool pending_ = false;
};
} // namespace
void validateWatchTarget(const WatchTarget &target) { validate(target); }
std::unique_ptr<DirectoryChanges> windowsDirectoryChanges(const WatchTarget &target) {
    return std::make_unique<WindowsChanges>(target);
}
} // namespace fileweaver::desktop
