#pragma once

#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <CLI/Encoding.hpp>
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace fileweaver::cli {
// The CRT's narrow argc can differ from the Unicode command line's argument count.
class Arguments {
  public:
    Arguments(int count, char **values) : count_(count), values_(values) {
#ifdef _WIN32
        int wide_count = 0;
        const auto release = [](wchar_t **pointer) { LocalFree(static_cast<void *>(pointer)); };
        std::unique_ptr<wchar_t *, decltype(release)> wide(
            CommandLineToArgvW(GetCommandLineW(), &wide_count), release);
        if (!wide) {
            throw std::runtime_error("Cannot read Unicode command line");
        }
        storage_.reserve(static_cast<std::size_t>(wide_count));
        for (int index = 0; index < wide_count; ++index) {
            storage_.push_back(CLI::narrow(wide.get()[index]));
        }
        for (auto &argument : storage_) {
            pointers_.push_back(argument.data());
        }
        pointers_.push_back(nullptr);
        count_ = wide_count;
        values_ = pointers_.data();
#endif
    }
    [[nodiscard]] int count() const noexcept { return count_; }
    Arguments(const Arguments &) = delete;
    Arguments &operator=(const Arguments &) = delete;
    [[nodiscard]] char **values() const noexcept { return values_; }

  private:
    int count_;
    char **values_;
    std::vector<std::string> storage_;
    std::vector<char *> pointers_;
};

// Native consoles consume UTF-16. Redirected output remains UTF-8 bytes.
class ConsoleOutput {
  public:
    explicit ConsoleOutput(std::ostream &stream, bool error) : stream_(stream) {
#ifdef _WIN32
        handle_ = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(handle_, &mode)) {
            original_ = stream_.rdbuf(buffer_.rdbuf());
        }
#else
        (void)error;
#endif
    }
    ConsoleOutput(const ConsoleOutput &) = delete;
    ConsoleOutput &operator=(const ConsoleOutput &) = delete;
    ~ConsoleOutput() {
        if (original_) {
            stream_.rdbuf(original_);
        }
    }
    bool finish() {
#ifdef _WIN32
        if (original_) {
            stream_.rdbuf(original_);
            original_ = nullptr;
            const auto text = buffer_.str();
            if (text.empty()) {
                return true;
            }
            if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
                return false;
            }
            const auto bytes = static_cast<int>(text.size());
            const int size =
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, nullptr, 0);
            if (size == 0) {
                return false;
            }
            std::wstring wide(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, wide.data(),
                                    size) != size) {
                return false;
            }
            std::wstring console_text;
            for (const wchar_t character : wide) {
                if (character == L'\n') {
                    console_text += L'\r';
                }
                console_text += character;
            }
            wide = std::move(console_text);
            std::size_t offset = 0;
            while (offset < wide.size()) {
                auto count = (std::min)(wide.size() - offset, std::size_t{8192});
                const auto last = wide[offset + count - 1];
                if (last >= 0xD800 && last <= 0xDBFF) {
                    --count;
                }
                DWORD written = 0;
                if (!WriteConsoleW(handle_, wide.data() + offset, static_cast<DWORD>(count),
                                   &written, nullptr) ||
                    written == 0) {
                    return false;
                }
                offset += written;
            }
        }
#endif
        return static_cast<bool>(stream_);
    }

  private:
    std::ostream &stream_;
    std::ostringstream buffer_;
    std::streambuf *original_ = nullptr;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif
};
} // namespace fileweaver::cli
