#include <cstdio>
#include <string>
#include <windows.h>
int main() {
    const std::wstring command = GetCommandLineW();
    if (command.find(L"slow.txt") != std::wstring::npos) {
        Sleep(10000);
        return 0;
    }
    if (command.find(L"crash.txt") != std::wstring::npos) {
        return 3;
    }
    if (command.find(L"oversize.txt") != std::wstring::npos) {
        const std::string text(std::size_t{600} * 1024, 'x');
        return std::fwrite(text.data(), 1, text.size(), stdout) == text.size() ? 0 : 1;
    }
    if (command.find(L"malformed.txt") != std::wstring::npos) {
        std::fputs("invalid JSON", stdout);
        return 0;
    }
    std::fputs("{\"ok\":true,\"kind\":\"text\",\"text\":\"synthetic helper\",\"truncated\":false}",
               stdout);
    return 0;
}
