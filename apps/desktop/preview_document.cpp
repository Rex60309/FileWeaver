#include "local_file.hpp"
#include "pdf_preview.hpp"
#include "preview.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringConverter>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <windows.h>

namespace fileweaver::desktop {
namespace {
struct Close {
    void operator()(HANDLE value) const {
        if (value && value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }
};
using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, Close>;
[[noreturn]] void ioError() {
    throw std::runtime_error("Preview file I/O failed (Windows error " +
                             std::to_string(GetLastError()) + ")");
}
QByteArray readFile(const QString &path, bool whole_file, bool &truncated) {
    const auto syntax = fileLocationPolicyError(path);
    const auto inspected = syntax.isEmpty() ? inspectLocalFile(path, {}) : syntax;
    if (!inspected.isEmpty()) {
        throw std::runtime_error(inspected.toStdString());
    }
    const Handle file(
        CreateFileW(path.toStdWString().c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE) {
        ioError();
    }
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (!GetFileInformationByHandle(file.get(), &before)) {
        ioError();
    }
    if ((before.dwFileAttributes & (static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY) |
                                    static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT) |
                                    static_cast<DWORD>(FILE_ATTRIBUTE_OFFLINE))) != 0) {
        throw std::runtime_error("Preview target is not an available regular file");
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size) || size.QuadPart < 0) {
        ioError();
    }
    constexpr qint64 document_limit = qint64{8} * 1024 * 1024;
    constexpr qint64 text_limit = qint64{64} * 1024;
    if (whole_file && size.QuadPart > document_limit) {
        throw std::runtime_error("Document exceeds 8 MiB preview limit");
    }
    const auto count = static_cast<DWORD>(
        std::min<qint64>(size.QuadPart, whole_file ? document_limit : text_limit));
    QByteArray result(static_cast<qsizetype>(count), Qt::Uninitialized);
    DWORD offset = 0;
    while (offset < count) {
        DWORD got = 0;
        if (!ReadFile(file.get(), result.data() + offset, count - offset, &got, nullptr)) {
            ioError();
        }
        if (got == 0) {
            break;
        }
        offset += got;
    }
    result.resize(offset);
    truncated = !whole_file && size.QuadPart > text_limit;
    if (!GetFileInformationByHandle(file.get(), &after)) {
        ioError();
    }
    if (offset != count || before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow ||
        CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) != 0) {
        throw std::runtime_error("File changed during preview; select it again");
    }
    return result;
}
void validateImageSize(const QSize &size) {
    if (!size.isValid() || size.width() < 1 || size.height() < 1 || size.width() > 8192 ||
        size.height() > 8192 ||
        static_cast<qint64>(size.width()) * size.height() > qint64{16} * 1024 * 1024) {
        throw std::runtime_error("Invalid image or image exceeds pixel/dimension limits");
    }
}
void validateGifFirstFrame(const QByteArray &bytes) {
    if (bytes.size() < 13 || (!bytes.startsWith("GIF87a") && !bytes.startsWith("GIF89a"))) {
        throw std::runtime_error("Invalid GIF header");
    }
    const auto dimension = [&bytes](qsizetype offset) {
        return static_cast<int>(static_cast<unsigned char>(bytes[offset])) +
               256 * static_cast<int>(static_cast<unsigned char>(bytes[offset + 1]));
    };
    const QSize canvas(dimension(6), dimension(8));
    validateImageSize(canvas);
    const auto packed = static_cast<unsigned char>(bytes[10]);
    qsizetype position = 13;
    if ((packed & 0x80U) != 0) {
        position +=
            static_cast<qsizetype>(std::size_t{3} * (std::size_t{1} << ((packed & 7U) + 1U)));
    }
    if (position > bytes.size()) {
        throw std::runtime_error("Incomplete GIF color table");
    }
    // Skip bounded extension sub-blocks without interpreting their payloads.
    // Inspect only the first image descriptor; never enumerate animation frames.
    while (position < bytes.size()) {
        const auto tag = static_cast<unsigned char>(bytes[position++]);
        if (tag == 0x2c) {
            if (bytes.size() - position < 9) {
                throw std::runtime_error("Incomplete GIF image descriptor");
            }
            const QSize frame(dimension(position + 4), dimension(position + 6));
            validateImageSize(frame);
            if (dimension(position) + frame.width() > canvas.width() ||
                dimension(position + 2) + frame.height() > canvas.height()) {
                throw std::runtime_error("GIF first frame exceeds its canvas");
            }
            return;
        }
        if (tag != 0x21 || position == bytes.size()) {
            throw std::runtime_error("GIF has no supported first frame");
        }
        ++position; // Extension label, not an instruction or external resource.
        for (;;) {
            if (position == bytes.size()) {
                throw std::runtime_error("Incomplete GIF extension");
            }
            const auto count = static_cast<unsigned char>(bytes[position++]);
            if (count == 0) {
                break;
            }
            if (count > bytes.size() - position) {
                throw std::runtime_error("Incomplete GIF extension payload");
            }
            position += count;
        }
    }
    throw std::runtime_error("GIF has no first frame");
}
QString textPreview(QByteArray bytes, bool &truncated) {
    QString text;
    if (bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff")) {
        const bool little = bytes.startsWith("\xff\xfe");
        bytes.remove(0, 2);
        if ((bytes.size() % 2) != 0) {
            if (!truncated) {
                throw std::runtime_error("Incomplete UTF-16 text");
            }
            bytes.chop(1);
        }
        if (truncated && bytes.size() >= 2) {
            const auto a = static_cast<unsigned char>(bytes[bytes.size() - 2]),
                       b = static_cast<unsigned char>(bytes[bytes.size() - 1]);
            const auto last =
                little ? static_cast<unsigned int>(a) | (static_cast<unsigned int>(b) << 8U)
                       : (static_cast<unsigned int>(a) << 8U) | static_cast<unsigned int>(b);
            if (last >= 0xd800 && last <= 0xdbff) {
                bytes.chop(2);
            }
        }
        QStringDecoder decoder(little ? QStringConverter::Utf16LE : QStringConverter::Utf16BE,
                               QStringConverter::Flag::Stateless);
        text = decoder(bytes);
        if (decoder.hasError()) {
            throw std::runtime_error("Invalid UTF-16 text");
        }
    } else {
        if (bytes.startsWith("\xef\xbb\xbf")) {
            bytes.remove(0, 3);
        }
        for (int i = 0; truncated && !bytes.isValidUtf8() && i < 3; ++i) {
            bytes.chop(1);
        }
        if (!bytes.isValidUtf8()) {
            throw std::runtime_error("Preview supports UTF-8 or BOM UTF-16 text only");
        }
        text = QString::fromUtf8(bytes);
    }
    for (const auto ch : text) {
        if ((ch.unicode() < 32 && ch != u'\n' && ch != u'\r' && ch != u'\t') ||
            ch.unicode() == 127) {
            throw std::runtime_error("Binary/control data cannot be previewed as text");
        }
    }
    int lines = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] == u'\n' && ++lines == 2000) {
            text.truncate(i);
            truncated = true;
            break;
        }
    }
    return text;
}
} // namespace
QByteArray previewDocument(const PreviewRequest &request) {
    const auto &path = request.path;
    QJsonObject reply;
    try {
        const auto extension = QFileInfo(path).suffix().toLower();
        const bool image = extension == u"png" || extension == u"jpg" || extension == u"jpeg" ||
                           extension == u"gif";
        const bool pdf = extension == u"pdf";
        const QStringList text_types{
            QStringLiteral("txt"),  QStringLiteral("md"),  QStringLiteral("markdown"),
            QStringLiteral("json"), QStringLiteral("xml"), QStringLiteral("csv"),
            QStringLiteral("c"),    QStringLiteral("h"),   QStringLiteral("cpp"),
            QStringLiteral("hpp"),  QStringLiteral("cc"),  QStringLiteral("cxx"),
            QStringLiteral("hxx"),  QStringLiteral("py"),  QStringLiteral("js"),
            QStringLiteral("jsx"),  QStringLiteral("ts"),  QStringLiteral("tsx"),
            QStringLiteral("java"), QStringLiteral("cs"),  QStringLiteral("rs"),
            QStringLiteral("go"),   QStringLiteral("rb"),  QStringLiteral("sh"),
            QStringLiteral("ps1"),  QStringLiteral("bat"), QStringLiteral("cmd"),
            QStringLiteral("cmake")};
        if (!image && !pdf && !text_types.contains(extension)) {
            throw std::runtime_error(
                "Unsupported preview type; use confirmed opening when supported or copy the path");
        }
        bool truncated = false;
        const auto bytes = readFile(path, image || pdf, truncated);
        reply[QStringLiteral("ok")] = true;
        if (!image && !pdf) {
            reply[QStringLiteral("kind")] = QStringLiteral("text");
            reply[QStringLiteral("text")] = textPreview(bytes, truncated);
            reply[QStringLiteral("truncated")] = truncated;
        } else {
            if (extension == u"gif") {
                validateGifFirstFrame(bytes);
            }
            const auto hash = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            reply[QStringLiteral("hash")] = hash;
            reply[QStringLiteral("first_frame")] = extension == u"gif";
            reply[QStringLiteral("first_page")] = pdf;
            if (hash == request.cached_hash) {
                reply[QStringLiteral("kind")] = QStringLiteral("cached");
            } else {
                QImage thumbnail;
                if (pdf) {
                    thumbnail = renderPdfFirstPage(bytes);
                } else {
                    QImageReader::setAllocationLimit(64);
                    QBuffer input;
                    input.setData(bytes);
                    input.open(QIODevice::ReadOnly);
                    QImageReader reader(&input, extension == u"png"   ? "png"
                                                : extension == u"gif" ? "gif"
                                                                      : "jpeg");
                    reader.setAutoDetectImageFormat(false);
                    const auto size = reader.size();
                    validateImageSize(size);
                    auto scaled = size.scaled(256, 256, Qt::KeepAspectRatio);
                    scaled.setWidth(std::max(1, scaled.width()));
                    scaled.setHeight(std::max(1, scaled.height()));
                    reader.setScaledSize(scaled);
                    thumbnail = reader.read();
                    if (thumbnail.isNull()) {
                        throw std::runtime_error("Corrupt or unsupported image");
                    }
                    thumbnail =
                        thumbnail.scaled(scaled, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_RGBA8888);
                }
                const auto length = thumbnail.width() * thumbnail.height() * 4;
                const QByteArray raw(reinterpret_cast<const char *>(thumbnail.constBits()), length);
                reply[QStringLiteral("kind")] = QStringLiteral("image");
                reply[QStringLiteral("width")] = thumbnail.width();
                reply[QStringLiteral("height")] = thumbnail.height();
                reply[QStringLiteral("pixels")] = QString::fromLatin1(raw.toBase64());
            }
        }
    } catch (const std::exception &error) {
        reply = {};
        reply[QStringLiteral("ok")] = false;
        reply[QStringLiteral("message")] = QString::fromUtf8(error.what()).left(1024);
    }
    return QJsonDocument(reply).toJson(QJsonDocument::Compact);
}
} // namespace fileweaver::desktop
