#pragma once
#include "preview.hpp"
#include <QObject>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
namespace fileweaver::desktop {
class PreviewController final : public QObject {
    Q_OBJECT
  public:
    using Read = std::function<PreviewReply(const PreviewRequest &, const std::stop_token &)>;
    explicit PreviewController(Read read = {}, QObject *parent = nullptr);
    ~PreviewController() override;
    void submit(QString path);
    void clear();
  signals:
    void completed(const fileweaver::desktop::PreviewReply &reply);

  private:
    struct Job {
        std::uint64_t generation;
        QString path;
        std::stop_source cancellation;
    };
    struct Completion {
        std::uint64_t generation;
        PreviewReply reply;
    };
    struct Entry {
        QString path;
        QString hash;
        QImage image;
    };
    void run(const std::stop_token &stop);
    void deliver();
    void post(std::uint64_t generation, PreviewReply reply);
    Read read_;
    std::uint64_t generation_ = 0;
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<Job> pending_;
    std::optional<Completion> completion_;
    std::stop_source active_;
    bool queued_ = false;
    bool clear_cache_ = false;
    std::vector<Entry> cache_; // Worker only. Max 8 x 256 x 256 RGBA (<4 MiB).
    std::jthread worker_;
};
} // namespace fileweaver::desktop
Q_DECLARE_METATYPE(fileweaver::desktop::PreviewReply)
