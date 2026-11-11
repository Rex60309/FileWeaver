#pragma once
#include <QByteArray>
#include <QImage>
namespace fileweaver::desktop {
// Trusted helper only: bounded in-memory bytes, no viewer/actions/password input.
[[nodiscard]] QImage renderPdfFirstPage(const QByteArray &bytes);
} // namespace fileweaver::desktop
