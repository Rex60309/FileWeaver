#pragma once
#include "fileweaver/application/index.hpp"
#include "fileweaver/application/search.hpp"
#include <QObject>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
namespace fileweaver::desktop {
[[nodiscard]] QString managedIndexDatabase();
[[nodiscard]] std::vector<SearchResult> desktopSearchFiles(const SearchRequest &request,
                                                           const std::stop_token &stop,
                                                           const QString &managed_database);
[[nodiscard]] IndexSummary buildDesktopIndex(const IndexRequest &request,
                                             const std::function<std::int64_t()> &clock,
                                             const std::stop_token &stop);
struct IndexApi {
    std::function<IndexSummary(const IndexRequest &, const std::function<std::int64_t()> &,
                               const std::stop_token &)>
        build;
    std::function<std::int64_t()> utc;
};
[[nodiscard]] IndexApi windowsIndexApi();
struct IndexReply {
    bool ok = false;
    bool cancelled = false;
    QString database;
    QString message;
    IndexSummary summary{};
};
// One active job and one completion. Reject submissions until GUI delivery.
class IndexController final : public QObject {
    Q_OBJECT
  public:
    explicit IndexController(IndexApi api = windowsIndexApi(), QObject *parent = nullptr);
    ~IndexController() override;
    [[nodiscard]] bool submit(IndexRequest request);
    void cancel();
    [[nodiscard]] bool isBusy() const { return busy_; }
  signals:
    void completed(const fileweaver::desktop::IndexReply &reply);

  private:
    void run(const std::stop_token &stop);
    void deliver();
    IndexApi api_;
    bool busy_ = false; // GUI thread only.
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<IndexRequest> pending_;
    std::optional<IndexReply> completion_;
    std::stop_source cancellation_;
    std::jthread worker_;
};
} // namespace fileweaver::desktop
Q_DECLARE_METATYPE(fileweaver::desktop::IndexReply)
