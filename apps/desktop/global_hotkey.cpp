#include "global_hotkey.hpp"
#include <QCoreApplication>
#include <QThread>
#include <stdexcept>
#include <utility>
#include <windows.h>

namespace fileweaver::desktop {
namespace {
unsigned long registrationResult(BOOL success) {
    if (success != 0) {
        return ERROR_SUCCESS;
    }
    const auto error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}
} // namespace
HotkeyApi windowsHotkeyApi() {
    return {
        [](const HotkeyBinding &binding) -> unsigned long {
            return registrationResult(RegisterHotKey(nullptr, binding.identifier, binding.modifiers,
                                                     binding.virtual_key));
        },
        [](int id) -> unsigned long { return registrationResult(UnregisterHotKey(nullptr, id)); }};
}

GlobalHotkey::GlobalHotkey(HotkeyApi api, QObject *parent) : QObject(parent), api_(std::move(api)) {
    if (QCoreApplication::instance() == nullptr || !api_.register_key || !api_.unregister_key) {
        throw std::invalid_argument("Global hotkey requires registration and cleanup adapters");
    }
    Q_ASSERT(QThread::currentThread() == QCoreApplication::instance()->thread());
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    const auto error = disable();
    if (!error.isEmpty()) {
        qWarning("%s", qPrintable(error));
    }
}

QString GlobalHotkey::enable(const QKeySequence &sequence) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (enabled_) {
        return QStringLiteral("請先停用快捷鍵，再修改組合。");
    }
    if (sequence.count() != 1) {
        return QStringLiteral("請輸入一組快捷鍵，不支援連續按鍵組合。");
    }
    const auto combination = sequence[0];
    const auto modifiers = combination.keyboardModifiers();
    if ((modifiers & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) != 0 ||
        (modifiers & (Qt::ControlModifier | Qt::AltModifier)) == 0) {
        return QStringLiteral("請包含 Ctrl 或 Alt，可搭配 Shift；不支援 Windows 鍵。");
    }
    const auto key = combination.key();
    if (key == Qt::Key_F12 ||
        !((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9) ||
          (key >= Qt::Key_F1 && key <= Qt::Key_F24) || key == Qt::Key_Space)) {
        return QStringLiteral("請搭配英文字母、數字、空白或 F1–F24（不含系統保留的 F12）。");
    }
    const unsigned int native_modifiers =
        MOD_NOREPEAT | (modifiers.testFlag(Qt::ControlModifier) ? MOD_CONTROL : 0U) |
        (modifiers.testFlag(Qt::AltModifier) ? MOD_ALT : 0U) |
        (modifiers.testFlag(Qt::ShiftModifier) ? MOD_SHIFT : 0U);
    const unsigned int native_key = key == Qt::Key_Space ? VK_SPACE
                                    : key >= Qt::Key_F1 && key <= Qt::Key_F24
                                        ? VK_F1 + static_cast<unsigned int>(key - Qt::Key_F1)
                                        : static_cast<unsigned int>(key);
    const auto error = api_.register_key({identifier, native_modifiers, native_key});
    if (error != ERROR_SUCCESS) {
        return error == ERROR_HOTKEY_ALREADY_REGISTERED
                   ? QStringLiteral("這組快捷鍵已被占用，請修改組合後重新啟用。")
                   : QStringLiteral("無法啟用快捷鍵（Windows 錯誤 %1），請修改組合後重試。")
                         .arg(error);
    }
    modifiers_ = native_modifiers & ~static_cast<unsigned int>(MOD_NOREPEAT);
    key_ = native_key;
    enabled_ = true;
    return {};
}

QString GlobalHotkey::disable() {
    Q_ASSERT(QThread::currentThread() == thread());
    if (enabled_) {
        const auto error = api_.unregister_key(identifier);
        if (error != ERROR_SUCCESS) {
            return QStringLiteral("無法停用快捷鍵（Windows 錯誤 %1），請重試或關閉程式。")
                .arg(error);
        }
        enabled_ = false;
    }
    return {};
}

bool GlobalHotkey::isEnabled() const { return enabled_; }

bool GlobalHotkey::nativeEventFilter(const QByteArray &type, void *message, qintptr *result) {
    if (!enabled_ || (type != "windows_dispatcher_MSG" && type != "windows_generic_MSG") ||
        message == nullptr) {
        return false;
    }
    const auto &event = *static_cast<const MSG *>(message);
    if (event.hwnd != nullptr || event.message != WM_HOTKEY || event.wParam != identifier ||
        LOWORD(event.lParam) != modifiers_ || HIWORD(event.lParam) != key_) {
        return false;
    }
    if (result != nullptr) {
        *result = 0;
    }
    emit activated();
    return true;
}
} // namespace fileweaver::desktop
