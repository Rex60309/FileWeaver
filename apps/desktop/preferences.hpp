#pragma once
#include "fileweaver/core/fuzzy.hpp"
#include <QObject>
#include <QString>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
namespace fileweaver::desktop {
struct Preferences {
    bool enabled = false;
    FuzzyOptions fuzzy{};
    QString hotkey = QStringLiteral("Ctrl+Alt+Space");
    int font = 10;
    int mode = 0;
    int sort = -1;
    int limit = 100;
    bool descending = false;
};
struct PreferencesReply {
    bool ok = false;
    Preferences value{};
    QString error{};
    bool loaded = false;
};
[[nodiscard]] PreferencesReply readPreferences(const QString &path);
[[nodiscard]] PreferencesReply writePreferences(const QString &path, const Preferences &value);
class PreferencesController final : public QObject {
    Q_OBJECT
  public:
    explicit PreferencesController(QObject *parent = nullptr);
    ~PreferencesController() override;
    void load(QString path);
    void save(QString path, Preferences value);
  signals:
    void completed(const fileweaver::desktop::PreferencesReply &reply);

  private:
    struct Job {
        QString path;
        std::optional<Preferences> value;
        std::uint64_t generation;
    };
    struct Completion {
        std::uint64_t generation;
        PreferencesReply reply;
    };
    void run(const std::stop_token &stop);
    void deliver();
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<Job> pending_;
    std::optional<Completion> completion_;
    std::uint64_t generation_ = 0;
    bool queued_ = false;
    std::jthread worker_;
};
} // namespace fileweaver::desktop
Q_DECLARE_METATYPE(fileweaver::desktop::PreferencesReply)
