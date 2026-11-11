#include "preview_controller.hpp"
#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <exception>
#include <utility>
namespace fileweaver::desktop {
PreviewController::PreviewController(Read read, QObject *parent)
    : QObject(parent), read_(std::move(read)) {
    if (!read_) {
        const auto path = std::filesystem::path((QCoreApplication::applicationDirPath() +
                                                 QStringLiteral("/fileweaver-preview-worker.exe"))
                                                    .toStdWString());
        read_ = [path](const auto &request, const auto &stop) {
            return runPreview(request, stop, {path});
        };
    }
    qRegisterMetaType<PreviewReply>();
    worker_ = std::jthread([this](const auto &stop) { run(stop); });
}
PreviewController::~PreviewController() {
    worker_.request_stop();
    {
        const std::lock_guard lock(mutex_);
        active_.request_stop();
        pending_.reset();
    }
    ready_.notify_all();
    worker_.join();
}
void PreviewController::submit(QString path) {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        const std::lock_guard lock(mutex_);
        active_.request_stop();
        completion_.reset();
        pending_ = Job{++generation_, std::move(path), {}};
    }
    ready_.notify_one();
}
void PreviewController::clear() {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        const std::lock_guard lock(mutex_);
        active_.request_stop();
        completion_.reset();
        clear_cache_ = true;
        pending_ = Job{++generation_, {}, {}};
    }
    ready_.notify_one();
}
void PreviewController::post(std::uint64_t generation, PreviewReply reply) {
    const std::lock_guard lock(mutex_);
    completion_ = Completion{generation, std::move(reply)};
    if (!queued_) {
        queued_ = true;
        QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
    }
}
void PreviewController::deliver() {
    std::optional<Completion> value;
    {
        const std::lock_guard lock(mutex_);
        queued_ = false;
        value = std::move(completion_);
        completion_.reset();
    }
    if (value && value->generation == generation_) {
        emit completed(value->reply);
    }
}
void PreviewController::run(const std::stop_token &stop) {
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
        active_ = job.cancellation;
        const bool clear = std::exchange(clear_cache_, false);
        lock.unlock();
        if (clear) {
            cache_.clear();
        }
        if (job.path.isEmpty()) {
            cache_.clear();
            continue;
        }
        PreviewReply reply;
        try {
            auto entry = std::find_if(cache_.begin(), cache_.end(),
                                      [&](const auto &value) { return value.path == job.path; });
            const auto hash = entry == cache_.end() ? QString{} : entry->hash;
            reply = read_({job.path, hash}, job.cancellation.get_token());
            if (job.cancellation.stop_requested()) {
                continue;
            }
            if (reply.ok && reply.cached) {
                if (entry == cache_.end() || entry->hash != reply.hash) {
                    reply = {false, QStringLiteral("縮圖快取驗證失敗，請重新選取。")};
                } else {
                    reply.image = entry->image;
                }
            }
            if (reply.ok && !reply.image.isNull()) {
                if (entry != cache_.end()) {
                    cache_.erase(entry);
                }
                cache_.push_back({job.path, reply.hash, reply.image});
                while (cache_.size() > 8) {
                    cache_.erase(cache_.begin());
                }
            }
        } catch (const std::exception &error) {
            reply = {false, QString::fromUtf8(error.what()).left(1024)};
        } catch (...) {
            reply = {false, QStringLiteral("預覽失敗，未開啟外部程式。")};
        }
        if (!stop.stop_requested() && !job.cancellation.stop_requested()) {
            post(job.generation, std::move(reply));
        }
    }
}
} // namespace fileweaver::desktop
