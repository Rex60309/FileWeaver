#include "index_controller.hpp"
#include "directory_changes.hpp"
#include "file_open.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include <QDir>
#include <QMetaObject>
#include <QStandardPaths>
#include <QThread>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <windows.h>
namespace fileweaver::desktop {
namespace {
struct Close {
    void operator()(HANDLE handle) const {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};
using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, Close>;
std::optional<DWORD> existingAttributes(const std::wstring &path) {
    const auto attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return attrs;
    }
    const auto error = GetLastError();
    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
        throw std::runtime_error("Cannot inspect index storage (Windows error " +
                                 std::to_string(error) + ")");
    }
    return std::nullopt;
}
QString text(const std::filesystem::path &path) {
    return QDir::toNativeSeparators(QString::fromStdWString(path.native()));
}
void directory(const QString &path, bool allow_missing) {
    QString current = path.left(3);
    bool missing = false;
    const auto inspect = [&](const QString &part) {
        const auto attrs = GetFileAttributesW(part.toStdWString().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (allow_missing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
                missing = true;
                return;
            }
            throw std::runtime_error("Cannot inspect index folder (Windows error " +
                                     std::to_string(error) + ")");
        }
        if (missing || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attrs & (static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT) |
                      static_cast<DWORD>(FILE_ATTRIBUTE_OFFLINE))) != 0) {
            throw std::runtime_error("Index folders/ancestors must be available local directories "
                                     "without reparse points");
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
}
void locations(const IndexRequest &request) {
    if (!request.root.is_absolute() || !request.database.is_absolute()) {
        throw std::runtime_error("Choose absolute local folder and index paths");
    }
    const auto root = text(request.root);
    const auto database = text(request.database);
    const auto probe = root + (root.endsWith(u'\\') ? QString{} : QStringLiteral("\\")) +
                       QStringLiteral("__index_probe__");
    if (!fileLocationPolicyError(probe).isEmpty() || !fileLocationPolicyError(database).isEmpty()) {
        throw std::runtime_error("Choose unambiguous absolute local folder and index paths");
    }
    for (const auto &path : {root, database}) {
        const auto drive = GetDriveTypeW(path.left(3).toStdWString().c_str());
        if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK &&
            drive != DRIVE_CDROM) {
            throw std::runtime_error("Indexing requires an available local drive");
        }
    }
    const auto prefix = root.endsWith(u'\\') ? root : root + u'\\';
    if (database.startsWith(prefix, Qt::CaseInsensitive)) {
        throw std::runtime_error("選取的資料夾包含程式索引儲存位置，請改選其子資料夾。");
    }
    directory(root, false);
    directory(text(request.database.parent_path()), true);
}
WatchTarget target(const IndexRequest &request) {
    const auto path = request.root.u8string();
    RootInfo root;
    root.path.assign(path.begin(), path.end());
    return {request.database, std::move(root)};
}
void cancelled(const std::stop_token &stop) {
    if (stop.stop_requested()) {
        throw std::runtime_error("Index creation cancelled");
    }
}
} // namespace
QString managedIndexDatabase() {
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base.isEmpty() ? QString{} : base + QStringLiteral("/catalog/index.sqlite");
}
std::vector<SearchResult> desktopSearchFiles(const SearchRequest &request,
                                             const std::stop_token &stop,
                                             const QString &managed_database) {
    // Only the managed, not-yet-created catalog is an empty first-run view.
    // This check runs on SearchController's worker, never on the GUI thread.
    if (QString::fromStdWString(request.database.native()) == managed_database) {
        std::error_code error;
        const bool exists = std::filesystem::exists(request.database, error);
        if (error) {
            throw std::runtime_error("Cannot inspect managed search index");
        }
        if (!exists) {
            return {};
        }
    }
    return search_files(request, stop);
}
IndexSummary buildDesktopIndex(const IndexRequest &request,
                               const std::function<std::int64_t()> &clock,
                               const std::stop_token &stop) {
    cancelled(stop);
    if (!clock) {
        throw std::invalid_argument("An index clock is required");
    }
    locations(request);
    for (const auto *suffix : {L"", L"-wal", L"-shm", L"-journal"}) {
        const auto path = request.database.native() + suffix;
        const auto attrs = existingAttributes(path);
        if (attrs && (*attrs & (static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT) |
                                static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY) |
                                static_cast<DWORD>(FILE_ATTRIBUTE_OFFLINE))) != 0) {
            throw std::runtime_error(
                "Index storage/sidecars must be available regular files without reparse points");
        }
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(request.database, error);
    if (error) {
        throw std::runtime_error("Cannot inspect index storage");
    }
    if (exists) {
        validateWatchTarget(target(request));
        const auto size = std::filesystem::file_size(request.database, error);
        if (error) {
            throw std::runtime_error("Cannot inspect index file size");
        }
        // A failed first initialization may leave an empty, data-free file.
        if (size == 0) {
            for (const auto *suffix : {L"-wal", L"-shm", L"-journal"}) {
                const auto sidecar = request.database.native() + suffix;
                if (existingAttributes(sidecar)) {
                    throw std::runtime_error(
                        "Empty catalog has existing sidecars; no files were removed");
                }
            }
            initialize_database(request.database);
        }
        if (catalog_status(request.database, stop).schema_version != 3) {
            throw std::runtime_error(
                "Managed index requires schema v3; no automatic migration was performed");
        }
    } else {
        for (const auto *suffix : {L"-wal", L"-shm", L"-journal"}) {
            const auto sidecar = request.database.native() + suffix;
            if (existingAttributes(sidecar)) {
                throw std::runtime_error(
                    "Index sidecars exist without their catalog; no files were removed");
            }
        }
        std::filesystem::create_directories(request.database.parent_path(), error);
        if (error) {
            throw std::runtime_error("Cannot create managed index folder");
        }
        locations(request);
        cancelled(stop);
        {
            const Handle created(CreateFileW(
                request.database.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (created.get() == INVALID_HANDLE_VALUE) {
                throw std::runtime_error(
                    "Cannot reserve new index; storage changed or is not writable");
            }
        }
        initialize_database(request.database);
    }
    cancelled(stop);
    validateWatchTarget(target(request));
    auto next = request;
    // Re-adding an existing root retains its saved policy and canonical spelling.
    for (const auto &root : list_roots(request.database, stop)) {
        const auto stored =
            std::filesystem::path(std::u8string(root.path.begin(), root.path.end()));
        if (text(stored).compare(text(request.root), Qt::CaseInsensitive) == 0) {
            std::error_code identity_error;
            const bool same = std::filesystem::equivalent(stored, request.root, identity_error);
            if (identity_error) {
                throw std::runtime_error("Cannot verify existing source folder identity");
            }
            if (!same) {
                continue;
            }
            if (!root.policy_known || !root.maximum_depth) {
                throw std::runtime_error(
                    "Existing root has no saved scan policy; re-index explicitly with CLI first");
            }
            next.root = stored;
            next.expected_root_id = root.root_id;
            next.scan.maximum_depth = static_cast<std::size_t>(*root.maximum_depth);
            next.scan.excluded_paths.clear();
            for (const auto &excluded : root.exclusions) {
                next.scan.excluded_paths.emplace_back(
                    std::u8string(excluded.begin(), excluded.end()));
            }
            break;
        }
    }
    return index_directory(next, clock, stop);
}
IndexApi windowsIndexApi() {
    return {buildDesktopIndex, [] {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                    .count();
            }};
}
IndexController::IndexController(IndexApi api, QObject *parent)
    : QObject(parent), api_(std::move(api)) {
    if (!api_.build || !api_.utc) {
        throw std::invalid_argument("Index adapters and clock are required");
    }
    qRegisterMetaType<IndexReply>();
    worker_ = std::jthread([this](const auto &stop) { run(stop); });
}
IndexController::~IndexController() {
    worker_.request_stop();
    cancel();
    ready_.notify_all();
    worker_.join();
}
bool IndexController::submit(IndexRequest request) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (busy_) {
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        cancellation_ = {};
        pending_ = std::move(request);
    }
    busy_ = true;
    ready_.notify_one();
    return true;
}
void IndexController::cancel() {
    const std::lock_guard lock(mutex_);
    cancellation_.request_stop();
}
void IndexController::run(const std::stop_token &stop) {
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop, [this] { return pending_.has_value(); });
        if (stop.stop_requested()) {
            return;
        }
        if (!pending_) {
            continue;
        }
        auto request = std::move(*pending_);
        pending_.reset();
        const auto token = cancellation_.get_token();
        lock.unlock();
        IndexReply reply;
        reply.database = QString::fromStdWString(request.database.native());
        try {
            reply.summary = api_.build(request, api_.utc, token);
            reply.cancelled = reply.summary.scan.cancelled;
            reply.ok = !reply.cancelled && reply.summary.scan.complete;
            if (!reply.cancelled && !reply.summary.scan.complete) {
                reply.message =
                    QStringLiteral(
                        "掃描未完成，未提交本次檔案變更；最後有效索引保留。略過 %1／錯誤 %2。")
                        .arg(reply.summary.scan.skipped)
                        .arg(reply.summary.scan.errors);
            }
        } catch (const std::exception &error) {
            reply.cancelled = token.stop_requested();
            reply.message = QString::fromUtf8(error.what()).left(1024);
        } catch (...) {
            reply.cancelled = token.stop_requested();
            reply.message = QStringLiteral("索引工作發生未預期錯誤。");
        }
        if (!stop.stop_requested()) {
            const std::lock_guard completion_lock(mutex_);
            completion_ = std::move(reply);
            QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
        }
    }
}
void IndexController::deliver() {
    std::optional<IndexReply> reply;
    {
        const std::lock_guard lock(mutex_);
        reply = std::move(completion_);
        completion_.reset();
    }
    if (reply) {
        busy_ = false;
        emit completed(*reply);
    }
}
} // namespace fileweaver::desktop
