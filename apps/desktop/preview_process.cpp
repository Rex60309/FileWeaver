#include "local_file.hpp"
#include "preview.hpp"
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <windows.h>

namespace fileweaver::desktop {
namespace {
struct Close {
    void operator()(HANDLE value) const {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};
using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, Close>;
[[noreturn]] void failure(const char *message) {
    throw std::runtime_error(std::string(message) + " (Windows error " +
                             std::to_string(GetLastError()) + ")");
}
class Attributes {
  public:
    Attributes(const std::array<HANDLE, 2> &handles) : handles_(handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0 || bytes > 65536) {
            failure("Cannot size preview process attributes");
        }
        storage_.resize((bytes + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
        value_ = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(value_, 1, 0, &bytes)) {
            value_ = nullptr;
            failure("Cannot initialize preview process attributes");
        }
        if (!UpdateProcThreadAttribute(value_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       static_cast<void *>(handles_.data()), sizeof(handles_),
                                       nullptr, nullptr)) {
            DeleteProcThreadAttributeList(value_);
            value_ = nullptr;
            failure("Cannot restrict preview inherited handles");
        }
    }
    ~Attributes() {
        if (value_) {
            DeleteProcThreadAttributeList(value_);
        }
    }
    Attributes(const Attributes &) = delete;
    Attributes &operator=(const Attributes &) = delete;
    LPPROC_THREAD_ATTRIBUTE_LIST get() const { return value_; }

  private:
    std::array<HANDLE, 2> handles_;
    std::vector<std::max_align_t> storage_;
    LPPROC_THREAD_ATTRIBUTE_LIST value_ = nullptr;
};
class Child {
  public:
    explicit Child(HANDLE value) : process_(value) {}
    ~Child() {
        if (process_ && WaitForSingleObject(process_.get(), 0) == WAIT_TIMEOUT) {
            TerminateProcess(process_.get(), 1);
            WaitForSingleObject(process_.get(), 5000);
        }
    }
    HANDLE get() const { return process_.get(); }

  private:
    Handle process_;
};
bool hashValid(const QString &hash) {
    if (hash.size() != 64) {
        return false;
    }
    return std::all_of(hash.begin(), hash.end(), [](QChar c) {
        return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
    });
}
} // namespace
PreviewReply parsePreview(const QByteArray &payload) {
    if (payload.isEmpty() || payload.size() > qsizetype{512} * 1024) {
        return {false, QStringLiteral("預覽程序輸出超限或不完整。")};
    }
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {false, QStringLiteral("預覽程序輸出無效。")};
    }
    const auto object = document.object();
    PreviewReply reply;
    if (!object.value(QStringLiteral("ok")).isBool()) {
        reply.message = QStringLiteral("預覽程序缺少狀態。");
        return reply;
    }
    if (!object.value(QStringLiteral("ok")).toBool()) {
        reply.message = object.value(QStringLiteral("message"))
                            .toString(QStringLiteral("無法預覽此檔案。"))
                            .left(1024);
        return reply;
    }
    const auto kind = object.value(QStringLiteral("kind")).toString();
    if (kind == u"text") {
        const auto value = object.value(QStringLiteral("text"));
        if (!value.isString() ||
            (value.toString().size() > 65536 || value.toString().count(u'\n') >= 2000)) {
            reply.message = QStringLiteral("文字預覽超限。");
            return reply;
        }
        reply.text = value.toString();
        reply.truncated = object.value(QStringLiteral("truncated")).toBool();
        reply.ok = true;
    } else if (kind == u"image" || kind == u"cached") {
        const auto first_frame = object.value(QStringLiteral("first_frame"));
        if (!first_frame.isUndefined() && !first_frame.isBool()) {
            reply.message = QStringLiteral("縮圖畫面標記無效。");
            return reply;
        }
        reply.first_frame = first_frame.toBool();
        const auto first_page = object.value(QStringLiteral("first_page"));
        if ((!first_page.isUndefined() && !first_page.isBool()) ||
            (reply.first_frame && first_page.toBool())) {
            reply.message = QStringLiteral("縮圖頁面標記無效。");
            return reply;
        }
        reply.first_page = first_page.toBool();
        reply.hash = object.value(QStringLiteral("hash")).toString();
        if (!hashValid(reply.hash)) {
            reply.message = QStringLiteral("縮圖驗證碼無效。");
            return reply;
        }
        if (kind == u"cached") {
            reply.cached = true;
            reply.ok = true;
        } else {
            const auto width = object.value(QStringLiteral("width")).toInt(0),
                       height = object.value(QStringLiteral("height")).toInt(0);
            if (width < 1 || height < 1 || width > 256 || height > 256) {
                reply.message = QStringLiteral("縮圖尺寸超限。");
                return reply;
            }
            const auto encoded = object.value(QStringLiteral("pixels")).toString().toLatin1();
            const auto decoded =
                QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded || decoded.decoded.size() != qsizetype{width} * height * 4) {
                reply.message = QStringLiteral("縮圖資料無效。");
                return reply;
            }
            reply.image = QImage(width, height, QImage::Format_RGBA8888);
            if (reply.image.isNull()) {
                reply.message = QStringLiteral("無法配置縮圖。");
                return reply;
            }
            std::memcpy(reply.image.bits(), decoded.decoded.constData(),
                        static_cast<std::size_t>(decoded.decoded.size()));
            reply.ok = true;
        }
    } else {
        reply.message = QStringLiteral("未知預覽格式。");
        return reply;
    }
    reply.message =
        reply.first_page ? QStringLiteral("PDF 第一頁靜態預覽；不提供腳本、表單、附件或連結互動。")
        : reply.first_frame
            ? QStringLiteral("GIF 首張畫面預覽（不播放動畫）；唯讀且不執行文件指令。")
        : reply.truncated
            ? QStringLiteral("僅顯示檔案前 64 KiB／2000 行以內；不執行程式碼或文件指令。")
            : QStringLiteral("唯讀預覽；不執行程式碼或文件指令。");
    return reply;
}
PreviewReply runPreview(const PreviewRequest &request, const std::stop_token &stop,
                        const PreviewProcess &process) {
    try {
        if (stop.stop_requested()) {
            return {false, QStringLiteral("已取消預覽。")};
        }
        const auto policy = fileLocationPolicyError(request.path);
        if (!policy.isEmpty()) {
            return {false, policy};
        }
        if (process.executable.empty() || !process.now) {
            throw std::invalid_argument("Preview worker and clock are required");
        }
        if (!request.cached_hash.isEmpty() && !hashValid(request.cached_hash)) {
            throw std::invalid_argument("Invalid cached thumbnail hash");
        }
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE read_raw = nullptr, write_raw = nullptr;
        if (!CreatePipe(&read_raw, &write_raw, &security, 0)) {
            failure("Cannot create preview output pipe");
        }
        Handle output(read_raw), writer(write_raw);
        if (!SetHandleInformation(output.get(), HANDLE_FLAG_INHERIT, 0)) {
            failure("Cannot isolate preview pipe");
        }
        Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (input.get() == INVALID_HANDLE_VALUE) {
            failure("Cannot create preview input");
        }
        Handle job(CreateJobObjectW(nullptr, nullptr));
        if (!job) {
            failure("Cannot create preview resource job");
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                                  JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                                  JOB_OBJECT_LIMIT_PROCESS_TIME;
        limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 20000000;
        limits.ProcessMemoryLimit = SIZE_T{192} * 1024U * 1024U;
        if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits))) {
            failure("Cannot set preview resource limits");
        }
        const std::array<HANDLE, 2> inherited{writer.get(), input.get()};
        Attributes attributes(inherited);
        STARTUPINFOEXW start{};
        start.StartupInfo.cb = sizeof(start);
        start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        start.StartupInfo.hStdInput = input.get();
        start.StartupInfo.hStdOutput = writer.get();
        start.StartupInfo.hStdError = writer.get();
        start.lpAttributeList = attributes.get();
        const auto exe = process.executable.native();
        const auto directory = process.executable.parent_path().native();
        if (exe.find(L'"') != std::wstring::npos) {
            throw std::invalid_argument("Invalid preview worker path");
        }
        auto command = L"\"" + exe + L"\" \"" + request.path.toStdWString() + L"\" \"" +
                       (request.cached_hash.isEmpty() ? std::wstring(L"-")
                                                      : request.cached_hash.toStdWString()) +
                       L"\"";
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
                            nullptr, directory.c_str(), &start.StartupInfo, &info)) {
            failure("Cannot start preview worker");
        }
        Child child(info.hProcess);
        Handle thread(info.hThread);
        if (!AssignProcessToJobObject(job.get(), child.get())) {
            failure("Cannot isolate preview worker");
        }
        if (ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
            failure("Cannot resume preview worker");
        }
        writer.reset();
        const auto deadline = process.now() + std::chrono::seconds(5);
        QByteArray payload;
        std::array<char, 16384> buffer{};
        for (;;) {
            if (stop.stop_requested()) {
                TerminateJobObject(job.get(), 1);
                return {false, QStringLiteral("已取消預覽。")};
            }
            if (process.now() >= deadline) {
                TerminateJobObject(job.get(), 1);
                return {false, QStringLiteral("預覽超過 5 秒，已停止程序。")};
            }
            DWORD available = 0;
            if (!PeekNamedPipe(output.get(), nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() != ERROR_BROKEN_PIPE) {
                    failure("Cannot inspect preview output");
                }
                available = 0;
            }
            if (available > 0) {
                DWORD got = 0;
                if (!ReadFile(output.get(), buffer.data(),
                              std::min(available, static_cast<DWORD>(buffer.size())), &got,
                              nullptr)) {
                    failure("Cannot read preview output");
                }
                payload.append(buffer.data(), static_cast<qsizetype>(got));
                if (payload.size() > qsizetype{512} * 1024) {
                    TerminateJobObject(job.get(), 1);
                    return {false, QStringLiteral("預覽程序輸出超限，已停止。")};
                }
                continue;
            }
            const auto state = WaitForSingleObject(child.get(), 10);
            if (state == WAIT_OBJECT_0) {
                // Recheck the pipe after observing process exit; no writer remains.
                DWORD remaining = 0;
                if (PeekNamedPipe(output.get(), nullptr, 0, nullptr, &remaining, nullptr) &&
                    remaining > 0) {
                    continue;
                }
                DWORD code = 1;
                if (!GetExitCodeProcess(child.get(), &code)) {
                    failure("Cannot inspect preview exit");
                }
                if (code != 0) {
                    return {false, QStringLiteral("預覽程序失敗或觸及資源上限；未開啟外部程式。")};
                }
                return parsePreview(payload);
            }
            if (state != WAIT_TIMEOUT) {
                failure("Cannot wait for preview worker");
            }
        }
    } catch (const std::exception &error) {
        return {false, QString::fromUtf8(error.what()).left(1024)};
    }
}
} // namespace fileweaver::desktop
