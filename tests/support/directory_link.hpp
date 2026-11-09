#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace fileweaver::test {
struct DirectoryLink {
    std::filesystem::path target;
    std::filesystem::path link;
};
inline void directoryLink(const DirectoryLink &paths) {
    const auto &target = paths.target;
    const auto &link = paths.link;
#ifdef _WIN32
    struct Junction {
        DWORD tag;
        WORD length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
        std::array<wchar_t, 2048> paths;
    } data{};
    const std::wstring substitute = L"\\??\\" + target.native();
    const auto &display = target.native();
    if (substitute.size() + display.size() + 2 >= data.paths.size()) {
        throw std::runtime_error("Fixture junction path exceeds buffer");
    }
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.print_offset = static_cast<WORD>((substitute.size() + 1) * sizeof(wchar_t));
    data.print_length = static_cast<WORD>(display.size() * sizeof(wchar_t));
    data.length = static_cast<WORD>(8 + data.print_offset + data.print_length + sizeof(wchar_t));
    std::copy(substitute.begin(), substitute.end(), data.paths.begin());
    std::copy(display.begin(), display.end(), data.paths.begin() + substitute.size() + 1);
    if (!std::filesystem::create_directory(link)) {
        throw std::runtime_error("Fixture junction destination already exists");
    }
    HANDLE raw = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                             FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Fixture junction handle error: " +
                                 std::to_string(GetLastError()));
    }
    std::unique_ptr<void, decltype(&CloseHandle)> handle(raw, CloseHandle);
    DWORD returned = 0;
    if (DeviceIoControl(handle.get(), FSCTL_SET_REPARSE_POINT, &data,
                        static_cast<DWORD>(8 + data.length), nullptr, 0, &returned,
                        nullptr) == FALSE) {
        throw std::runtime_error("Fixture junction creation error: " +
                                 std::to_string(GetLastError()));
    }
#else
    std::filesystem::create_directory_symlink(target, link);
#endif
}
} // namespace fileweaver::test
