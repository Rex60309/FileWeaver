#include "watch_controller.hpp"
#include <QMetaObject>
#include <QThread>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace fileweaver::desktop {
WatchApi windowsWatchApi() {
    return {list_roots,
            windowsDirectoryChanges,
            [](const WatchTarget &target, const auto &clock, const auto &stop) {
                validateWatchTarget(target);
                return refresh_root({target.database, target.root.root_id,
                                     std::filesystem::path(std::u8string(target.root.path.begin(),
                                                                         target.root.path.end()))},
                                    clock, stop);
            },
            [] { return std::chrono::steady_clock::now(); },
            [] {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                    .count();
            },
            [](const std::stop_token &stop) {
                std::mutex mutex;
                std::condition_variable_any wake;
                std::unique_lock lock(mutex);
                wake.wait_for(lock, stop, std::chrono::milliseconds(200), [] { return false; });
            }};
}
WatchController::WatchController(WatchApi api, QObject *parent)
    : QObject(parent), api_(std::move(api)) {
    if (!api_.roots || !api_.source || !api_.sync || !api_.now || !api_.utc || !api_.idle) {
        throw std::invalid_argument("Watch adapters and clocks are required");
    }
    qRegisterMetaType<WatchReply>();
    worker_ = std::jthread([this](const std::stop_token &stop) { run(stop); });
}
WatchController::~WatchController() {
    worker_.request_stop();
    {
        const std::lock_guard lock(mutex_);
        active_stop_.request_stop();
        pending_.reset();
    }
    ready_.notify_all();
    worker_.join();
}
bool WatchController::submit(Kind kind, WatchTarget target) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (busy_) {
        return false;
    }
    {
        const std::lock_guard lock(mutex_);
        pending_ = Job{kind, std::move(target), ++generation_, {}};
        completion_.reset();
    }
    busy_ = true;
    ready_.notify_one();
    return true;
}
bool WatchController::load(const std::filesystem::path &database) {
    return submit(Kind::load, {database, {}});
}
bool WatchController::start(WatchTarget target) {
    if (target.root.root_id <= 0 || !target.root.policy_known) {
        return false;
    }
    return submit(Kind::watch, std::move(target));
}
bool WatchController::isBusy() const { return busy_; }
void WatchController::cancel() {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        const std::lock_guard lock(mutex_);
        active_stop_.request_stop();
        completion_.reset();
        pending_ = Job{Kind::stop, {}, ++generation_, {}};
    }
    busy_ = true;
    ready_.notify_one();
}
void WatchController::post(std::uint64_t generation, WatchReply reply) {
    const std::lock_guard lock(mutex_);
    completion_ = Completion{generation, std::move(reply)};
    if (!delivery_queued_) {
        delivery_queued_ = true;
        QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
    }
}
void WatchController::deliver() {
    std::optional<Completion> value;
    {
        const std::lock_guard lock(mutex_);
        delivery_queued_ = false;
        value = std::move(completion_);
        completion_.reset();
    }
    if (value && value->generation == generation_) {
        busy_ = value->reply.busy;
        emit updated(value->reply);
    }
}
void WatchController::run(const std::stop_token &stop) {
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
        try {
            if (job.kind == Kind::load) {
                WatchReply reply;
                reply.kind = WatchReplyKind::roots;
                reply.roots = api_.roots(job.target.database, job.cancellation.get_token());
                reply.message = QStringLiteral("請選擇已保存掃描設定的根目錄，再手動啟用監控。");
                post(job.generation, std::move(reply));
            } else if (job.kind == Kind::watch) {
                watch(job);
            } else {
                post(job.generation, {WatchReplyKind::stopped, {}, QStringLiteral("監控未啟用。")});
            }
        } catch (const std::exception &error) {
            post(job.generation,
                 {WatchReplyKind::stopped, {}, QString::fromUtf8(error.what()).left(4096)});
        } catch (...) {
            post(job.generation,
                 {WatchReplyKind::stopped, {}, QStringLiteral("監控工作發生未知錯誤，已停止。")});
        }
    }
}
void WatchController::watch(const Job &job) {
    const auto stop = job.cancellation.get_token();
    WatchReply reply;
    reply.busy = true;
    auto schedule = WatchSchedule(api_.now());
    auto attach_due = api_.now();
    std::unique_ptr<DirectoryChanges> source;
    while (!stop.stop_requested()) {
        try {
            if (!source) {
                if (api_.now() < attach_due) {
                    api_.idle(stop);
                    continue;
                }
                source = api_.source(job.target);
                if (!source) {
                    throw std::runtime_error("Notification adapter returned no source");
                }
                schedule.urgent(api_.now()); // Startup and every reattach require reconciliation.
                reply.message = QStringLiteral("通知已連線，正在重新核對索引…");
                post(job.generation, reply);
            }
            if (schedule.due(api_.now())) {
                reply.message = QStringLiteral("監控中：正在背景核對索引…");
                post(job.generation, reply);
                const auto result = api_.sync(job.target, api_.utc, stop);
                if (stop.stop_requested()) {
                    break;
                }
                const bool success = result.exit_code == 0 && result.index &&
                                     result.index->scan.complete && !result.index->scan.cancelled;
                schedule.completed(api_.now(), success);
                if (!success) {
                    auto error =
                        result.error.empty()
                            ? std::string("Reconciliation incomplete; previous index retained")
                            : result.error;
                    if (result.index) {
                        error += " (skipped=" + std::to_string(result.index->scan.skipped) +
                                 ", errors=" + std::to_string(result.index->scan.errors) + ")";
                    }
                    throw std::runtime_error(error);
                }
                const auto &index = result.index.value();
                ++reply.scans;
                reply.last_sync = api_.utc();
                reply.message =
                    QStringLiteral(
                        "監控中：已同步。新增 %1、更新 %2、Missing %3；略過 %4、錯誤 %5。")
                        .arg(index.added)
                        .arg(index.updated)
                        .arg(index.missing)
                        .arg(index.scan.skipped)
                        .arg(index.scan.errors);
                if (!index.missing_reconciled) {
                    reply.message +=
                        QStringLiteral(" Missing 尚未核對：有排除／略過項目，舊結果可能保留。");
                }
                post(job.generation, reply);
            }
            const auto change = source->poll(stop);
            if (change == DirectoryChange::cancelled) {
                break;
            }
            if (change == DirectoryChange::changed) {
                schedule.changed(api_.now());
            }
            if (change == DirectoryChange::overflow) {
                schedule.urgent(api_.now());
                reply.message = QStringLiteral("通知溢位／遺漏，將重新核對整個選定根目錄。");
                post(job.generation, reply);
            }
        } catch (const std::exception &error) {
            if (stop.stop_requested()) {
                break;
            }
            source.reset();
            attach_due = api_.now() + std::chrono::seconds(5);
            reply.message = QStringLiteral("監控／同步失敗；保留既有索引，5 秒後嘗試重新連線：") +
                            QString::fromUtf8(error.what()).left(4096);
            post(job.generation, reply);
        }
    }
    reply.kind = WatchReplyKind::stopped;
    reply.busy = false;
    reply.message = QStringLiteral("監控已停止。");
    post(job.generation, std::move(reply));
}
} // namespace fileweaver::desktop
