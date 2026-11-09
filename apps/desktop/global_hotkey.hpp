#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <functional>

namespace fileweaver::desktop {
struct HotkeyBinding {
    int identifier;
    unsigned int modifiers;
    unsigned int virtual_key;
};
// Zero means success. The adapter captures the OS error immediately on failure.
struct HotkeyApi {
    std::function<unsigned long(const HotkeyBinding &)> register_key;
    std::function<unsigned long(int)> unregister_key;
};
[[nodiscard]] HotkeyApi windowsHotkeyApi();

// GUI-thread-only owner of one explicitly enabled Windows registration.
class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalHotkey(HotkeyApi api = windowsHotkeyApi(), QObject *parent = nullptr);
    ~GlobalHotkey() override;
    [[nodiscard]] QString enable(const QKeySequence &sequence);
    [[nodiscard]] QString disable();
    [[nodiscard]] bool isEnabled() const;
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

  signals:
    void activated();

  private:
    HotkeyApi api_;
    unsigned int modifiers_ = 0;
    unsigned int key_ = 0;
    bool enabled_ = false;
    static constexpr int identifier = 0x4657;
};
} // namespace fileweaver::desktop
