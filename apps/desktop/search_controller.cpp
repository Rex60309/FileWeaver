#include "search_controller.hpp"
#include <QByteArrayView>
#include <QMetaObject>
#include <QThread>
#include <exception>
#include <stdexcept>
#include <utility>

namespace fileweaver::desktop {
SearchController::SearchController(Search search, QObject *parent)
    : QObject(parent), search_(std::move(search)) {
    if (!search_) {
        throw std::invalid_argument("Desktop search requires a search function");
    }
    qRegisterMetaType<SearchReply>();
    worker_ = std::jthread([this](const std::stop_token &stop) { run(stop); });
}

SearchController::~SearchController() {
    worker_.request_stop();
    {
        const std::lock_guard lock(mutex_);
        active_stop_.request_stop();
        pending_.reset();
    }
    ready_.notify_all();
    worker_.join(); // QObject remains alive until the worker stops posting to it.
}

void SearchController::submit(SearchRequest request) {
    Q_ASSERT(QThread::currentThread() == thread());
    ++generation_;
    {
        const std::lock_guard lock(mutex_);
        active_stop_.request_stop();
        completion_.reset();
        pending_ = Job{generation_, std::move(request), {}};
    }
    ready_.notify_one();
}

void SearchController::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    ++generation_;
    const std::lock_guard lock(mutex_);
    active_stop_.request_stop();
    pending_.reset();
    completion_.reset();
}

void SearchController::run(const std::stop_token &stop) {
    while (!stop.stop_requested()) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop, [this] { return pending_.has_value(); });
        if (stop.stop_requested()) {
            return;
        }
        if (!pending_) {
            continue;
        }
        auto job = std::move(*pending_);
        pending_.reset();
        active_stop_ = job.cancellation;
        lock.unlock();
        SearchReply reply;
        try {
            reply.results = search_(job.request, job.cancellation.get_token());
            for (const auto &result : reply.results) {
                const auto valid = [](const std::string &text) {
                    return !text.empty() && text.size() <= 131072 &&
                           text.find('\0') == std::string::npos &&
                           QByteArrayView(text.data(), static_cast<qsizetype>(text.size()))
                               .isValidUtf8();
                };
                if (!valid(result.name) || !valid(result.display_path)) {
                    throw std::runtime_error("索引包含無效的 UTF-8 檔名或路徑，請重新建立索引。");
                }
            }
        } catch (const std::exception &error) {
            reply.results.clear();
            reply.error = QString::fromUtf8(error.what()).left(4096);
        } catch (...) {
            reply.results.clear();
            reply.error = QStringLiteral("搜尋發生未知錯誤，請重新選擇索引後重試。");
        }
        lock.lock();
        if (stop.stop_requested()) {
            return;
        }
        completion_ = Completion{job.generation, std::move(reply)};
        const bool post = !delivery_queued_;
        delivery_queued_ = true;
        lock.unlock();
        if (post) {
            QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
        }
    }
}

void SearchController::deliver() {
    Q_ASSERT(QThread::currentThread() == thread());
    std::optional<Completion> ready_reply;
    {
        const std::lock_guard lock(mutex_);
        delivery_queued_ = false;
        ready_reply = std::move(completion_);
        completion_.reset();
    }
    if (ready_reply && ready_reply->generation == generation_) {
        emit completed(ready_reply->reply);
    }
}
} // namespace fileweaver::desktop
