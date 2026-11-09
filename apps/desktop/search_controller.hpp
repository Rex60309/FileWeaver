#pragma once

#include "fileweaver/application/search.hpp"
#include <QObject>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace fileweaver::desktop {
struct SearchReply {
    std::vector<SearchResult> results;
    QString error;
};

// GUI-thread facade; one persistent worker, one pending job, one staged reply.
class SearchController final : public QObject {
    Q_OBJECT
  public:
    using Search =
        std::function<std::vector<SearchResult>(const SearchRequest &, const std::stop_token &)>;
    explicit SearchController(Search search = fileweaver::search_files, QObject *parent = nullptr);
    ~SearchController() override;
    void submit(SearchRequest request);
    void cancel();

  signals:
    void completed(const fileweaver::desktop::SearchReply &reply);

  private:
    struct Job {
        std::uint64_t generation;
        SearchRequest request;
        std::stop_source cancellation;
    };
    struct Completion {
        std::uint64_t generation;
        SearchReply reply;
    };
    void run(const std::stop_token &stop);
    void deliver();
    Search search_;
    std::uint64_t generation_ = 0; // Only accessed by the GUI thread.
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<Job> pending_;
    std::optional<Completion> completion_;
    std::stop_source active_stop_;
    bool delivery_queued_ = false;
    std::jthread worker_;
};
} // namespace fileweaver::desktop

Q_DECLARE_METATYPE(fileweaver::desktop::SearchReply)
