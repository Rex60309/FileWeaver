#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stop_token>
namespace fileweaver::desktop {
struct PreviewRequest {
    QString path;
    QString cached_hash;
};
struct PreviewReply {
    bool ok = false;
    QString message;
    QString text{};
    QImage image{};
    QString hash{};
    bool cached = false;
    bool truncated = false;
    bool first_frame = false;
};
struct PreviewProcess {
    std::filesystem::path executable;
    std::function<std::chrono::steady_clock::time_point()> now = [] {
        return std::chrono::steady_clock::now();
    };
};
[[nodiscard]] QByteArray previewDocument(const PreviewRequest &request);
[[nodiscard]] PreviewReply runPreview(const PreviewRequest &request, const std::stop_token &stop,
                                      const PreviewProcess &process);
[[nodiscard]] PreviewReply parsePreview(const QByteArray &payload);
} // namespace fileweaver::desktop
