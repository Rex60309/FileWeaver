#pragma once
#include "directory_changes.hpp"
#include "fileweaver/application/refresh.hpp"
#include "fileweaver/core/watch_schedule.hpp"
#include <QObject>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace fileweaver::desktop {
struct WatchApi {
    std::function<std::vector<RootInfo>(const std::filesystem::path &, const std::stop_token &)>
        roots;
    std::function<std::unique_ptr<DirectoryChanges>(const WatchTarget &)> source;
    std::function<RefreshRootResult(const WatchTarget &, const std::function<std::int64_t()> &,
                                    const std::stop_token &)>
        sync;
    std::function<WatchSchedule::Time()> now;
    std::function<std::int64_t()> utc;
    std::function<void(const std::stop_token &)> idle;
};
[[nodiscard]] WatchApi windowsWatchApi();
enum class WatchReplyKind : std::uint8_t { roots, status, stopped };
struct WatchReply {
    WatchReplyKind kind = WatchReplyKind::status;
    std::vector<RootInfo> roots;
    QString message;
    bool busy = false;
    std::uint64_t scans = 0;
    std::optional<std::int64_t> last_sync = std::nullopt;
};
// One serialized catalog/notification/reconciliation worker and one staged reply.
class WatchController final : public QObject {
    Q_OBJECT
  public:
    explicit WatchController(WatchApi api = windowsWatchApi(), QObject *parent = nullptr);
    ~WatchController() override;
    [[nodiscard]] bool load(const std::filesystem::path &database);
    [[nodiscard]] bool start(WatchTarget target);
    void cancel();
    [[nodiscard]] bool isBusy() const;
  signals:
    void updated(const fileweaver::desktop::WatchReply &reply);

  private:
    enum class Kind : std::uint8_t { load, watch, stop };
    struct Job {
        Kind kind;
        WatchTarget target;
        std::uint64_t generation;
        std::stop_source cancellation;
    };
    struct Completion {
        std::uint64_t generation;
        WatchReply reply;
    };
    bool submit(Kind kind, WatchTarget target);
    void run(const std::stop_token &stop);
    void watch(const Job &job);
    void post(std::uint64_t generation, WatchReply reply);
    void deliver();
    WatchApi api_;
    std::uint64_t generation_ = 0;
    bool busy_ = false;
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<Job> pending_;
    std::optional<Completion> completion_;
    std::stop_source active_stop_;
    bool delivery_queued_ = false;
    std::jthread worker_;
};
} // namespace fileweaver::desktop
Q_DECLARE_METATYPE(fileweaver::desktop::WatchReply)
