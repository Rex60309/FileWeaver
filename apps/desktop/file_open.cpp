#include "file_open.hpp"
#include <QMetaObject>
#include <QStringList>
#include <QThread>
#include <array>
#include <exception>
#include <memory>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <windows.h>

namespace fileweaver::desktop {
namespace {
QString systemError(const QString &action, unsigned long error) {
    return action + QStringLiteral("（Windows 錯誤 %1）。").arg(error);
}

class ShellApartment {
  public:
    ShellApartment()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ShellApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    ShellApartment(const ShellApartment &) = delete;
    ShellApartment &operator=(const ShellApartment &) = delete;
    [[nodiscard]] QString error() const {
        return FAILED(result_) ? QStringLiteral("無法初始化 Windows 檔案介面（錯誤 %1）。")
                                     .arg(static_cast<unsigned long>(result_))
                               : QString{};
    }

  private:
    HRESULT result_;
};

QString revealFile(const QString &path) {
    const ShellApartment apartment;
    if (!apartment.error().isEmpty()) {
        return apartment.error();
    }
    const auto native = path.toStdWString();
    PIDLIST_ABSOLUTE raw = nullptr;
    const auto parsed = SHParseDisplayName(native.c_str(), nullptr, &raw, 0, nullptr);
    using ItemId =
        std::unique_ptr<std::remove_pointer_t<PIDLIST_ABSOLUTE>, decltype(&CoTaskMemFree)>;
    const ItemId item(raw, CoTaskMemFree);
    if (FAILED(parsed) || item == nullptr) {
        return QStringLiteral("無法取得檔案總管位置，請更新索引並檢查檔案（錯誤 %1）。")
            .arg(static_cast<unsigned long>(parsed));
    }
    const auto shown = SHOpenFolderAndSelectItems(item.get(), 0, nullptr, 0);
    return FAILED(shown) ? QStringLiteral("無法交由檔案總管顯示位置（錯誤 %1），請稍後重試。")
                               .arg(static_cast<unsigned long>(shown))
                         : QString{};
}

QString launchFile(const QString &path) {
    const ShellApartment apartment;
    if (!apartment.error().isEmpty()) {
        return apartment.error();
    }
    std::array<wchar_t, MAX_PATH + 1> directory{};
    const auto length = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));
    if (length == 0 || length >= directory.size()) {
        return QStringLiteral("無法取得 Windows 系統目錄，未送出開啟請求。");
    }
    const auto native = path.toStdWString();
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI | SEE_MASK_UNICODE;
    request.lpVerb = L"open";
    request.lpFile = native.c_str();
    request.lpDirectory = directory.data();
    request.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&request) == FALSE) {
        const auto error = GetLastError();
        if (error == ERROR_NO_ASSOCIATION) {
            return QStringLiteral("Windows 尚未設定這類檔案的預設程式，請先設定後重試。");
        }
        if (error == ERROR_CANCELLED) {
            return QStringLiteral("Windows 或使用者取消了開啟請求。");
        }
        return systemError(QStringLiteral("無法交由 Windows 開啟，請確認檔案與預設程式"), error);
    }
    return {};
}
} // namespace

QString fileOpenPolicyError(const QString &path) {
    auto error = fileLocationPolicyError(path);
    if (!error.isEmpty()) {
        return error;
    }
    const auto parts = path.mid(3).split(u'\\');
    const auto extension = parts.last().section(u'.', -1).toLower();
    const QStringList supported{
        QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("pdf"),
        QStringLiteral("docx"), QStringLiteral("xlsx"), QStringLiteral("pptx"),
        QStringLiteral("csv"),  QStringLiteral("png"),  QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("bmp"),  QStringLiteral("gif"),
        QStringLiteral("webp"), QStringLiteral("mp3"),  QStringLiteral("wav"),
        QStringLiteral("flac"), QStringLiteral("aac"),  QStringLiteral("m4a"),
        QStringLiteral("ogg"),  QStringLiteral("mp4"),  QStringLiteral("mkv"),
        QStringLiteral("avi"),  QStringLiteral("mov"),  QStringLiteral("webm")};
    if (!parts.last().contains(u'.') || !supported.contains(extension)) {
        return QStringLiteral("此版本只開啟支援的文件、圖片與影音；其他類型請複製路徑手動處理。");
    }
    return {};
}

FileOpenApi windowsFileOpenApi() { return {inspectLocalFile, launchFile}; }
FileOpenApi windowsRevealFileApi() { return {inspectLocalFile, revealFile}; }

namespace {
OpenReply performAction(const QString &path, const std::stop_token &stop, const FileOpenApi &api,
                        FileAction action) {
    const auto cancelled = [] {
        return OpenReply{OpenState::cancelled, QStringLiteral("已取消檔案操作。")};
    };
    if (stop.stop_requested()) {
        return cancelled();
    }
    const auto policy =
        action == FileAction::open ? fileOpenPolicyError(path) : fileLocationPolicyError(path);
    if (!policy.isEmpty()) {
        return {OpenState::failed, policy};
    }
    if (!api.inspect || !api.launch) {
        throw std::invalid_argument("File opening requires inspection and launch adapters");
    }
    const auto inspected = api.inspect(path, stop);
    if (stop.stop_requested()) {
        return cancelled();
    }
    if (!inspected.isEmpty()) {
        return {OpenState::failed, inspected};
    }
    const auto error = api.launch(path);
    if (error.isEmpty() && action == FileAction::reveal) {
        return {OpenState::handed_off,
                QStringLiteral("已請求檔案總管顯示並選取檔案；不代表視窗已切到前景。")};
    }
    return error.isEmpty()
               ? OpenReply{OpenState::handed_off,
                           QStringLiteral("已交由 Windows 預設程式處理；不代表檔案已成功顯示。")}
               : OpenReply{OpenState::failed, error};
}

} // namespace

OpenReply openFileWithApi(const QString &path, const std::stop_token &stop,
                          const FileOpenApi &api) {
    return performAction(path, stop, api, FileAction::open);
}
OpenReply revealFileWithApi(const QString &path, const std::stop_token &stop,
                            const FileOpenApi &api) {
    return performAction(path, stop, api, FileAction::reveal);
}
OpenReply revealSelectedFile(const QString &path, const std::stop_token &stop) {
    return revealFileWithApi(path, stop, windowsRevealFileApi());
}

OpenReply openSelectedFile(const QString &path, const std::stop_token &stop) {
    return openFileWithApi(path, stop, windowsFileOpenApi());
}

FileOpenController::FileOpenController(Open open, QObject *parent, Open reveal)
    : QObject(parent), open_(std::move(open)), reveal_(std::move(reveal)) {
    if (!open_ || !reveal_) {
        throw std::invalid_argument("File opening requires an open function");
    }
    qRegisterMetaType<OpenReply>();
    worker_ = std::jthread([this](const std::stop_token &stop) { run(stop); });
}

FileOpenController::~FileOpenController() {
    cancel();
    worker_.request_stop();
    ready_.notify_all();
    worker_.join();
}

bool FileOpenController::submit(QString path, FileAction action) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (busy_) {
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        operation_stop_ = std::stop_source{};
        pending_ = Job{std::move(path), action};
    }
    busy_ = true;
    ready_.notify_one();
    return true;
}

bool FileOpenController::isBusy() const { return busy_; }

void FileOpenController::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    const std::lock_guard lock(mutex_);
    operation_stop_.request_stop();
}

void FileOpenController::run(const std::stop_token &stop) {
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop, [this] { return pending_.has_value(); });
        if (stop.stop_requested()) {
            return;
        }
        if (!pending_) {
            continue;
        }
        const auto job = std::move(*pending_);
        pending_.reset();
        const auto operation_stop = operation_stop_.get_token();
        lock.unlock();
        OpenReply reply;
        try {
            reply = (job.action == FileAction::open ? open_ : reveal_)(job.path, operation_stop);
        } catch (const std::exception &error) {
            reply = {OpenState::failed, QString::fromUtf8(error.what()).left(4096)};
        } catch (...) {
            reply = {OpenState::failed,
                     QStringLiteral("檔案操作請求發生未知錯誤，請稍後手動重試。")};
        }
        if (!stop.stop_requested()) {
            QMetaObject::invokeMethod(
                this,
                [this, reply = std::move(reply)] {
                    busy_ = false;
                    emit completed(reply);
                },
                Qt::QueuedConnection);
        }
    }
}
} // namespace fileweaver::desktop
