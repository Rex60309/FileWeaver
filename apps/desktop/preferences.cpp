#include "preferences.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMetaObject>
#include <QSaveFile>
#include <QThread>
#include <stdexcept>
#include <utility>
namespace fileweaver::desktop {
namespace {
int integer(const QJsonObject &object, const QString &key, int low, int high) {
    const auto value = object.value(key);
    const auto number = value.toInt(low - 1);
    if (!value.isDouble() || value.toDouble() != number || number < low || number > high) {
        throw std::runtime_error("Invalid preference value");
    }
    return number;
}
QJsonObject encode(const Preferences &value) {
    QJsonObject object{{QStringLiteral("version"), 1}, {QStringLiteral("enabled"), value.enabled}};
    if (!value.enabled) {
        return object;
    }
    if (value.fuzzy.candidate_limit < 1 || value.fuzzy.candidate_limit > 10000) {
        throw std::runtime_error("Invalid preference candidate limit");
    }
    object[QStringLiteral("exact")] = value.fuzzy.weights.exact;
    object[QStringLiteral("prefix")] = value.fuzzy.weights.prefix;
    object[QStringLiteral("token")] = value.fuzzy.weights.token;
    object[QStringLiteral("subsequence")] = value.fuzzy.weights.subsequence;
    object[QStringLiteral("typo")] = value.fuzzy.weights.typo;
    object[QStringLiteral("path")] = value.fuzzy.weights.path;
    object[QStringLiteral("candidates")] = static_cast<int>(value.fuzzy.candidate_limit);
    object[QStringLiteral("hotkey")] = value.hotkey;
    object[QStringLiteral("font")] = value.font;
    object[QStringLiteral("mode")] = value.mode;
    object[QStringLiteral("sort")] = value.sort;
    object[QStringLiteral("limit")] = value.limit;
    object[QStringLiteral("descending")] = value.descending;
    return object;
}
Preferences decode(const QJsonObject &object) {
    if (integer(object, QStringLiteral("version"), 1, 1) != 1 ||
        !object.value(QStringLiteral("enabled")).isBool()) {
        throw std::runtime_error("Unsupported preference format");
    }
    Preferences value;
    value.enabled = object.value(QStringLiteral("enabled")).toBool();
    if (!value.enabled) {
        return value;
    }
    value.fuzzy.weights = {integer(object, QStringLiteral("exact"), 0, 10000),
                           integer(object, QStringLiteral("prefix"), 0, 10000),
                           integer(object, QStringLiteral("token"), 0, 10000),
                           integer(object, QStringLiteral("subsequence"), 0, 10000),
                           integer(object, QStringLiteral("typo"), 0, 10000),
                           integer(object, QStringLiteral("path"), 0, 10000)};
    value.fuzzy.candidate_limit =
        static_cast<std::size_t>(integer(object, QStringLiteral("candidates"), 1, 10000));
    const auto key = object.value(QStringLiteral("hotkey"));
    if (!key.isString() || key.toString().size() > 64 ||
        QKeySequence::fromString(key.toString(), QKeySequence::PortableText).isEmpty()) {
        throw std::runtime_error("Invalid saved hotkey");
    }
    value.hotkey = key.toString();
    value.font = integer(object, QStringLiteral("font"), 8, 24);
    value.mode = integer(object, QStringLiteral("mode"), 0, 6);
    value.sort = integer(object, QStringLiteral("sort"), -1, 3);
    value.limit = integer(object, QStringLiteral("limit"), 1, 1000);
    if (!object.value(QStringLiteral("descending")).isBool()) {
        throw std::runtime_error("Invalid saved sort direction");
    }
    value.descending = object.value(QStringLiteral("descending")).toBool();
    return value;
}
} // namespace
PreferencesReply readPreferences(const QString &path) {
    PreferencesReply reply;
    reply.loaded = true;
    try {
        QFile file(path);
        if (QFileInfo(path).isSymLink()) {
            throw std::runtime_error("Preference file must not be a symbolic link");
        }
        if (!file.exists()) {
            reply.ok = true;
            return reply;
        }
        if (!file.open(QIODevice::ReadOnly) || file.size() > 4096) {
            throw std::runtime_error("Cannot read preferences or preference file exceeds 4 KiB");
        }
        const auto bytes = file.read(4097);
        if (bytes.size() > 4096 || file.error() != QFileDevice::NoError) {
            throw std::runtime_error("Preference read failed");
        }
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            throw std::runtime_error("Invalid preference JSON");
        }
        reply.value = decode(document.object());
        reply.ok = true;
    } catch (const std::exception &error) {
        reply.error = QString::fromUtf8(error.what()).left(512);
    }
    return reply;
}
PreferencesReply writePreferences(const QString &path, const Preferences &value) {
    PreferencesReply reply;
    try {
        if (QFileInfo(path).isSymLink() || QFileInfo(path).isDir()) {
            throw std::runtime_error("Preference target must be a regular application file");
        }
        const auto object = encode(value);
        static_cast<void>(decode(object));
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            throw std::runtime_error("Cannot create preference directory");
        }
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
            !file.commit()) {
            throw std::runtime_error("Cannot atomically save preferences");
        }
        reply.value = value;
        reply.ok = true;
    } catch (const std::exception &error) {
        reply.error = QString::fromUtf8(error.what()).left(512);
    }
    return reply;
}
PreferencesController::PreferencesController(QObject *parent) : QObject(parent) {
    qRegisterMetaType<PreferencesReply>();
    worker_ = std::jthread([this](const auto &stop) { run(stop); });
}
PreferencesController::~PreferencesController() {
    worker_.request_stop();
    ready_.notify_all();
    worker_.join();
}
void PreferencesController::load(QString path) {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        const std::lock_guard lock(mutex_);
        completion_.reset();
        pending_ = Job{std::move(path), {}, ++generation_};
    }
    ready_.notify_one();
}
void PreferencesController::save(QString path, Preferences value) {
    Q_ASSERT(QThread::currentThread() == thread());
    {
        const std::lock_guard lock(mutex_);
        completion_.reset();
        pending_ = Job{std::move(path), std::move(value), ++generation_};
    }
    ready_.notify_one();
}
void PreferencesController::run(const std::stop_token &stop) {
    for (;;) {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, stop, [this] { return pending_.has_value(); });
        if (!pending_) {
            if (stop.stop_requested()) {
                return;
            }
            continue;
        }
        auto job = std::move(*pending_);
        pending_.reset();
        lock.unlock();
        if (stop.stop_requested() && !job.value) {
            continue;
        }
        auto reply =
            job.value ? writePreferences(job.path, job.value.value()) : readPreferences(job.path);
        lock.lock();
        completion_ = Completion{job.generation, std::move(reply)};
        if (!queued_ && !stop.stop_requested()) {
            queued_ = true;
            QMetaObject::invokeMethod(this, [this] { deliver(); }, Qt::QueuedConnection);
        }
    }
}
void PreferencesController::deliver() {
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
} // namespace fileweaver::desktop
