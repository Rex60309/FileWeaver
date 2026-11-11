#pragma once

#include "local_file.hpp"
#include <QObject>
#include <QString>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace fileweaver::desktop {
enum class FileAction : std::uint8_t { open, reveal };
enum class OpenState : std::uint8_t { handed_off, failed, cancelled };
struct OpenReply {
    OpenState state = OpenState::failed;
    QString message;
};
struct FileOpenApi {
    std::function<QString(const QString &, const std::stop_token &)> inspect;
    std::function<QString(const QString &)> launch;
};
[[nodiscard]] QString fileOpenPolicyError(const QString &path);
[[nodiscard]] FileOpenApi windowsFileOpenApi();
[[nodiscard]] FileOpenApi windowsRevealFileApi();
[[nodiscard]] OpenReply revealFileWithApi(const QString &path, const std::stop_token &stop,
                                          const FileOpenApi &api);
[[nodiscard]] OpenReply revealSelectedFile(const QString &path, const std::stop_token &stop);
[[nodiscard]] OpenReply openFileWithApi(const QString &path, const std::stop_token &stop,
                                        const FileOpenApi &api);
[[nodiscard]] OpenReply openSelectedFile(const QString &path, const std::stop_token &stop);

// Accepts one confirmed operation at a time; no waiting queue or automatic retry.
class FileOpenController final : public QObject {
    Q_OBJECT
  public:
    using Open = std::function<OpenReply(const QString &, const std::stop_token &)>;
    explicit FileOpenController(Open open = openSelectedFile, QObject *parent = nullptr,
                                Open reveal = revealSelectedFile);
    ~FileOpenController() override;
    [[nodiscard]] bool submit(QString path, FileAction action = FileAction::open);
    [[nodiscard]] bool isBusy() const;
    void cancel();

  signals:
    void completed(const fileweaver::desktop::OpenReply &reply);

  private:
    struct Job {
        QString path;
        FileAction action;
    };
    void run(const std::stop_token &stop);
    Open open_;
    Open reveal_;
    bool busy_ = false; // GUI thread only; released when its completion is delivered.
    std::mutex mutex_;
    std::condition_variable_any ready_;
    std::optional<Job> pending_;
    std::stop_source operation_stop_;
    std::jthread worker_;
};
} // namespace fileweaver::desktop

Q_DECLARE_METATYPE(fileweaver::desktop::OpenReply)
