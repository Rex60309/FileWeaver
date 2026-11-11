#include "pdf_preview.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <poppler-document.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>
#include <stdexcept>
#include <string>
namespace fileweaver::desktop {
namespace {
class Diagnostics {
  public:
    Diagnostics() { poppler::set_debug_error_function(receive, &count_); }
    ~Diagnostics() { poppler::set_debug_error_function(receive, nullptr); }
    Diagnostics(const Diagnostics &) = delete;
    Diagnostics &operator=(const Diagnostics &) = delete;
    int count() const { return count_; }

  private:
    static void receive(const std::string &, void *closure) {
        // Never forward document-derived diagnostics to stdout/stderr or logs.
        if (closure) {
            auto &count = *static_cast<int *>(closure);
            count = std::min(1000, count + 1);
        }
    }
    int count_ = 0;
};
} // namespace
QImage renderPdfFirstPage(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > qsizetype{8} * 1024 * 1024 ||
        !bytes.startsWith("%PDF-")) {
        throw std::runtime_error("Invalid PDF header or PDF exceeds 8 MiB");
    }
    // Library diagnostics are process-global; only the single-document helper
    // invokes this adapter. The capture outlives every Poppler object below.
    Diagnostics diagnostics;
    const std::unique_ptr<poppler::document> document(
        poppler::document::load_from_raw_data(bytes.constData(), static_cast<int>(bytes.size())));
    if (!document) {
        throw std::runtime_error("Cannot parse PDF; no document content was logged");
    }
    if (document->is_locked() || document->is_encrypted()) {
        throw std::runtime_error("Encrypted/password-protected PDF preview is unsupported");
    }
    const std::unique_ptr<poppler::page> page(document->create_page(0));
    if (!page || !poppler::page_renderer::can_render()) {
        throw std::runtime_error("PDF has no renderable first page");
    }
    const auto bounds = page->page_rect();
    for (const double value : {bounds.left(), bounds.top(), bounds.right(), bounds.bottom(),
                               bounds.width(), bounds.height()}) {
        if (!std::isfinite(value) || std::abs(value) > 14400.0) {
            throw std::runtime_error("PDF page geometry exceeds preview limits");
        }
    }
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0) {
        throw std::runtime_error("PDF page has invalid dimensions");
    }
    const double dpi = 72.0 * 256.0 / std::max(bounds.width(), bounds.height());
    if (!std::isfinite(dpi) || dpi <= 0.0 || dpi > 7200.0) {
        throw std::runtime_error("PDF page is too small or unsupported for preview");
    }
    poppler::page_renderer renderer;
    renderer.set_image_format(poppler::image::format_argb32);
    // Fixed raster slice bounds allocation independently of untrusted page sizes;
    // equal X/Y DPI preserves aspect ratio, with white padding as needed.
    const auto image = renderer.render_page(page.get(), dpi, dpi, 0, 0, 256, 256);
    if (diagnostics.count() != 0) {
        throw std::runtime_error("PDF parser reported diagnostics; preview unavailable (count " +
                                 std::to_string(diagnostics.count()) + ")");
    }
    if (!image.is_valid() || image.width() < 1 || image.width() > 256 || image.height() < 1 ||
        image.height() > 256 || image.format() != poppler::image::format_argb32 ||
        !image.const_data() || image.bytes_per_row() != image.width() * 4) {
        throw std::runtime_error("PDF renderer returned invalid or oversized pixels");
    }
    const QImage view(reinterpret_cast<const unsigned char *>(image.const_data()), image.width(),
                      image.height(), image.bytes_per_row(), QImage::Format_ARGB32);
    auto result = view.convertToFormat(QImage::Format_RGBA8888);
    if (result.isNull()) {
        throw std::runtime_error("Cannot allocate PDF preview pixels");
    }
    return result;
}
} // namespace fileweaver::desktop
