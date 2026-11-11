#pragma once
#include <QString>
#include <stop_token>
namespace fileweaver::desktop {
[[nodiscard]] QString fileLocationPolicyError(const QString &path);
[[nodiscard]] QString inspectLocalFile(const QString &path, const std::stop_token &stop);
} // namespace fileweaver::desktop
