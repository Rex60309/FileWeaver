#include "preferences.hpp"
#include "preview.hpp"
#include "preview_controller.hpp"
#include "preview_highlighter.hpp"
#include "search_window.hpp"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>
#include <QTreeView>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
using namespace fileweaver::desktop;
namespace {
template <class T> T *control(SearchWindow &window, const char *name) {
    auto *value = window.findChild<T *>(QString::fromLatin1(name));
    if (!value) {
        throw std::runtime_error("Missing phase2 control");
    }
    return value;
}
QString native(const QString &path) { return QDir::toNativeSeparators(path); }
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error("Cannot write synthetic fixture");
    }
}
PreviewProcess process(const char *name = "fileweaver-preview-worker.exe") {
    return {std::filesystem::path(
        (QCoreApplication::applicationDirPath() + u'/' + QString::fromLatin1(name))
            .toStdWString())};
}
std::vector<fileweaver::SearchResult> result() {
    return {{1, 1, "C:/synthetic/report.txt", "report.txt", 3}};
}
// Deterministic temporary PDF with a red first page and blue second page.
QByteArray pdfFixture(const QByteArray &geometry = "0 0 200 200", bool active = false,
                      bool encrypted = false) {
    const QByteArray red("1 0 0 rg 0 0 200 200 re f\n");
    const QByteArray blue("0 0 1 rg 0 0 200 200 re f\n");
    const auto stream = [](const QByteArray &data) {
        return QByteArray("<< /Length ") + QByteArray::number(data.size()) + ">>\nstream\n" + data +
               "endstream";
    };
    const QList<QByteArray> objects{
        QByteArray("<< /Type /Catalog /Pages 2 0 R ") +
            (active ? "/OpenAction << /S /JavaScript /JS (throw 123;) >> " : "") + ">>",
        "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
        QByteArray("<< /Type /Page /Parent 2 0 R /MediaBox [") + geometry +
            "] /Resources << >> /Contents 5 0 R >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 6 0 R >>",
        stream(red),
        stream(blue),
        QByteArray("<< /Filter /Standard /V 1 /R 2 /Length 40 /P -4 /O "
                   "<0000000000000000000000000000000000000000000000000000000000000000> /U "
                   "<0000000000000000000000000000000000000000000000000000000000000000> >>")};
    QByteArray out("%PDF-1.4\n");
    QList<qsizetype> offsets{0};
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const auto xref = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (qsizetype i = 1; i < offsets.size(); ++i) {
        out += QByteArray::number(offsets[i]).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R ";
    if (encrypted) {
        // Locked security dictionary fixture: intentionally no usable password.
        out += "/Encrypt 7 0 R /ID "
               "[<00112233445566778899aabbccddeeff><00112233445566778899aabbccddeeff>] ";
    }
    out += ">>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}
} // namespace
class Phase2Tests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultFontMatchesDisplayedPreference() {
        SearchWindow window;
        QCOMPARE(window.font().pointSize(),
                 control<QSpinBox>(window, "interfaceFontSize")->value());
        QCOMPARE(control<QPlainTextEdit>(window, "previewText")->font().pointSize(),
                 window.font().pointSize());
    }
    void previewTextLimitsEncodingAndNoExecution() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("日本語😀.cpp")));
        const QByteArray source = "int main() { return 0; } // fixture";
        write(path, source);
        auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.text, QString::fromUtf8(source));
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), source);
        unchanged.close();
        const auto large = native(temp.filePath(QStringLiteral("large.txt")));
        write(large, QByteArray(100000, 'a'));
        reply = runPreview({large, {}}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.truncated);
        QCOMPARE(reply.text.size(), qsizetype{65536});
        const auto lines = native(temp.filePath(QStringLiteral("lines.txt")));
        write(lines, QByteArray("first\n") + QByteArray(5000, '\n'));
        reply = runPreview({lines, {}}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.truncated);
        QVERIFY(reply.text.startsWith(QStringLiteral("first")));
        QVERIFY(reply.text.count(u'\n') < 2000);
        const auto utf16 = native(temp.filePath(QStringLiteral("utf16.txt")));
        write(utf16, QByteArray::fromHex("fffee5652c67"));
        reply = runPreview({utf16, {}}, {}, process());
        QVERIFY(reply.ok);
        QCOMPARE(reply.text, QStringLiteral("日本"));
        const auto binary = native(temp.filePath(QStringLiteral("binary.txt")));
        write(binary, QByteArray("a\0b", 3));
        QVERIFY(!runPreview({binary, {}}, {}, process()).ok);
        const auto invalid = native(temp.filePath(QStringLiteral("invalid.txt")));
        write(invalid, QByteArray::fromHex("fffffe"));
        QVERIFY(!runPreview({invalid, {}}, {}, process()).ok);
        const auto script = native(temp.filePath(QStringLiteral("untrusted.ps1")));
        write(script, "Remove-Item anything");
        reply = runPreview({script, {}}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("Remove-Item")));
        const auto unsupported = native(temp.filePath(QStringLiteral("unsupported.rar")));
        write(unsupported, "%PDF synthetic");
        QVERIFY(!runPreview({unsupported, {}}, {}, process()).ok);
        QVERIFY(
            !runPreview({native(temp.filePath(QStringLiteral("missing.txt"))), {}}, {}, process())
                 .ok);
        std::stop_source stopped;
        stopped.request_stop();
        QVERIFY(!runPreview({path, {}}, stopped.get_token(), process()).ok);
    }
    void sidebarTypesLimitAndCompactLayout() {
        std::mutex mutex;
        fileweaver::SearchRequest last;
        std::atomic<int> calls = 0;
        SearchWindow window([&](const auto &request, const auto &) {
            const std::lock_guard lock(mutex);
            last = request;
            ++calls;
            return result();
        });
        window.show();
        window.setDatabase(QStringLiteral("C:/synthetic/index.sqlite"));
        auto *view = control<QTreeView>(window, "results");
        QTRY_VERIFY(control<QGroupBox>(window, "typeFilters")->height() < 250);
        const auto before = view->height();
        QTest::mouseClick(control<QPushButton>(window, "compactSettings"), Qt::LeftButton);
        QTRY_VERIFY(view->height() > before);
        control<QSpinBox>(window, "visibleResultLimit")->setValue(1000);
        control<QCheckBox>(window, "typeFilter0")->setChecked(true);
        control<QCheckBox>(window, "typeFilter1")->setChecked(true);
        QTRY_VERIFY(calls.load() > 0);
        QTRY_VERIFY(([&] {
            const std::lock_guard lock(mutex);
            return last.limit == 1000 && last.filters.extensions.size() == 20;
        })());
        control<QCheckBox>(window, "typeFilter0")->setChecked(false);
        control<QCheckBox>(window, "typeFilter1")->setChecked(false);
        QTRY_VERIFY(([&] {
            const std::lock_guard lock(mutex);
            return last.filters.extensions.empty();
        })());
    }
    void imagePreviewUsesPaneWithoutEmptyTextEditor() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("preview.png")));
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY(image.save(path, "PNG"));
        SearchWindow window([&](const auto &, const auto &) {
            return std::vector<fileweaver::SearchResult>{
                {1, 1, path.toStdString(), "preview.png", 1}};
        });
        window.resize(1280, 900);
        window.show();
        QTest::mouseClick(control<QPushButton>(window, "compactSettings"), Qt::LeftButton);
        window.setDatabase(QStringLiteral("C:/synthetic/index.sqlite"));
        QTRY_VERIFY(control<QTreeView>(window, "results")->currentIndex().isValid());
        control<QCheckBox>(window, "previewEnabled")->setChecked(true);
        auto *picture = control<QLabel>(window, "previewImage");
        QTRY_VERIFY(!picture->pixmap().isNull());
        QVERIFY(picture->isVisible());
        QVERIFY(!control<QPlainTextEdit>(window, "previewText")->isVisible());
        const auto pixmap = picture->pixmap();
        QVERIFY(pixmap.width() > 256);
        QVERIFY(std::abs(pixmap.width() - pixmap.height() * 2) <= 2);
        QVERIFY(pixmap.width() <= picture->width());
        control<QCheckBox>(window, "previewEnabled")->setChecked(false);
        QVERIFY(picture->pixmap().isNull());
    }
    void jpegOrientationIsAppliedWithoutDistortion() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (int y = 0; y < 40; ++y) {
            for (int x = 40; x < 80; ++x) {
                image.setPixelColor(x, y, Qt::blue);
            }
        }
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "JPEG", 100));
        // EXIF little-endian orientation 6: rotate 90 degrees clockwise.
        const auto exif = QByteArray::fromHex(
            "ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        jpeg.insert(2, exif);
        const auto path = native(temp.filePath(QStringLiteral("oriented.jpg")));
        write(path, jpeg);
        const auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.image.height(), reply.image.width() * 2);
        QVERIFY(reply.image.pixelColor(reply.image.width() / 2, reply.image.height() / 4).red() >
                200);
        QVERIFY(
            reply.image.pixelColor(reply.image.width() / 2, reply.image.height() * 3 / 4).blue() >
            200);
    }
    void jpegMirrorOrientationIsApplied() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QImage image(80, 40, QImage::Format_RGB32);
        image.fill(Qt::red);
        for (int y = 0; y < 40; ++y) {
            for (int x = 40; x < 80; ++x) {
                image.setPixelColor(x, y, Qt::blue);
            }
        }
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "JPEG", 100));
        jpeg.insert(
            2, QByteArray::fromHex(
                   "ffe1002245786966000049492a0008000000010012010300010000000200000000000000"));
        const auto path = native(temp.filePath(QStringLiteral("mirrored.jpg")));
        write(path, jpeg);
        const auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.image.width(), reply.image.height() * 2);
        QVERIFY(reply.image.pixelColor(reply.image.width() / 4, reply.image.height() / 2).blue() >
                200);
        QVERIFY(
            reply.image.pixelColor(reply.image.width() * 3 / 4, reply.image.height() / 2).red() >
            200);
    }
    void previewImagesCacheChecksumAndCorruption() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto png = native(temp.filePath(QStringLiteral("picture.png")));
        QImage image(16, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray red_bytes;
        QBuffer red_buffer(&red_bytes);
        QVERIFY(red_buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&red_buffer, "PNG"));
        image.fill(Qt::blue);
        QByteArray blue_bytes;
        QBuffer blue_buffer(&blue_bytes);
        QVERIFY(blue_buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&blue_buffer, "PNG"));
        const auto equal_size = std::max(red_bytes.size(), blue_bytes.size());
        red_bytes.append(QByteArray(equal_size - red_bytes.size(), '\0'));
        blue_bytes.append(QByteArray(equal_size - blue_bytes.size(), '\0'));
        write(png, red_bytes);
        auto reply = runPreview({png, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!reply.image.isNull());
        QCOMPARE(reply.image.pixelColor(0, 0), QColor(Qt::red));
        const auto hash = reply.hash;
        reply = runPreview({png, hash}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.cached);
        const auto old_size = QFileInfo(png).size();
        const auto file = std::filesystem::path(png.toStdWString());
        const auto old_time = std::filesystem::last_write_time(file);
        image.fill(Qt::blue);
        write(png, blue_bytes);
        QCOMPARE(QFileInfo(png).size(), old_size);
        std::filesystem::last_write_time(file, old_time);
        reply = runPreview({png, hash}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(!reply.cached);
        QVERIFY(reply.hash != hash);
        QCOMPARE(reply.image.pixelColor(0, 0), QColor(Qt::blue));
        const auto jpeg = native(temp.filePath(QStringLiteral("picture.jpg")));
        QVERIFY(image.save(jpeg, "JPEG"));
        reply = runPreview({jpeg, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!reply.image.isNull());
        const auto corrupt = native(temp.filePath(QStringLiteral("corrupt.png")));
        write(corrupt, "not an image");
        QVERIFY(!runPreview({corrupt, {}}, {}, process()).ok);
        const auto giant = native(temp.filePath(QStringLiteral("giant.png")));
        write(giant, QByteArray(8 * 1024 * 1024 + 1, 'x'));
        QVERIFY(!runPreview({giant, {}}, {}, process()).ok);
        const auto pixels = native(temp.filePath(QStringLiteral("pixels.png")));
        QImage huge(8193, 1, QImage::Format_RGB32);
        huge.fill(Qt::red);
        QVERIFY(huge.save(pixels, "PNG"));
        QVERIFY(!runPreview({pixels, {}}, {}, process()).ok);
        const auto thin = native(temp.filePath(QStringLiteral("thin.png")));
        QImage line(4096, 1, QImage::Format_RGB32);
        line.fill(Qt::green);
        QVERIFY(line.save(thin, "PNG"));
        reply = runPreview({thin, {}}, {}, process());
        QVERIFY(reply.ok);
        QCOMPARE(reply.image.height(), 1);
    }
    void gifPreviewShowsOnlyFirstFrameAndRetainsLimits() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("動畫 日本語.gif")));
        const auto header = QByteArray::fromHex("47494638396101000100800000ff00000000ff");
        const auto red = QByteArray::fromHex("21f90400010000002c0000000001000100000202440100");
        const auto blue = QByteArray::fromHex("21f90400010000002c00000000010001000002024c0100");
        const auto animated = header + red + blue + QByteArray::fromHex("3b");
        write(path, animated);
        {
            QImageReader fixture(path, "gif");
            QCOMPARE(fixture.read().pixelColor(0, 0), QColor(Qt::red));
            QCOMPARE(fixture.read().pixelColor(0, 0), QColor(Qt::blue));
        }
        auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.image.pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(reply.first_frame);
        QVERIFY(reply.message.contains(QStringLiteral("GIF 首張畫面")));
        QVERIFY(reply.message.contains(QStringLiteral("不播放動畫")));
        const auto hash = reply.hash;
        auto invalid_flag = QJsonDocument::fromJson(previewDocument({path, {}})).object();
        invalid_flag[QStringLiteral("first_frame")] = QStringLiteral("true");
        QVERIFY(!parsePreview(QJsonDocument(invalid_flag).toJson()).ok);
        reply = runPreview({path, hash}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.cached);
        QVERIFY(reply.first_frame);
        QVERIFY(reply.message.contains(QStringLiteral("GIF 首張畫面")));
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), animated);
        unchanged.close();
        write(path, header + red + QByteArray::fromHex("3b"));
        reply = runPreview({path, hash}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(!reply.cached);
        QVERIFY(reply.hash != hash);
        QCOMPARE(reply.image.pixelColor(0, 0), QColor(Qt::red));
        for (const auto &invalid :
             {QByteArray("GIF89a"), header.left(18), header + QByteArray::fromHex("3b"),
              header + QByteArray::fromHex("2c0000"), header + QByteArray::fromHex("21f9"),
              header + QByteArray::fromHex("21f90400"),
              header + QByteArray::fromHex("21f90400000000")}) {
            write(path, invalid);
            QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        }
        auto gif87 = header;
        gif87[4] = '7';
        write(path, gif87 + red.mid(8) + QByteArray::fromHex("3b"));
        QVERIFY(runPreview({path, {}}, {}, process()).ok);
        auto outside_canvas = red;
        outside_canvas[9] = char{1};
        write(path, header + outside_canvas + QByteArray::fromHex("3b"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        auto zero_canvas = header;
        zero_canvas[6] = char{0};
        write(path, zero_canvas + red + QByteArray::fromHex("3b"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        auto pixel_limit = header;
        pixel_limit[6] = char{0};
        pixel_limit[7] = char{32};
        pixel_limit[8] = char{0};
        pixel_limit[9] = char{32};
        write(path, pixel_limit + red + QByteArray::fromHex("3b"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        auto oversized_dimensions = header;
        oversized_dimensions[6] = char{1};
        oversized_dimensions[7] = char{32};
        write(path, oversized_dimensions + red + QByteArray::fromHex("3b"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        auto oversized_frame = red;
        oversized_frame[13] = char{1};
        oversized_frame[14] = char{32};
        write(path, header + oversized_frame + QByteArray::fromHex("3b"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, QByteArray(8 * 1024 * 1024 + 1, 'x'));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        std::stop_source stopped;
        stopped.request_stop();
        QVERIFY(!runPreview({path, {}}, stopped.get_token(), process()).ok);
    }
    void pdfFirstPageLimitsCacheAndUntrustedActions() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("首頁 日本語.pdf")));
        const auto bytes = pdfFixture();
        write(path, bytes);
        auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(reply.first_page);
        QVERIFY(!reply.first_frame);
        QVERIFY(reply.message.contains(QStringLiteral("PDF 第一頁")));
        QVERIFY(reply.image.width() <= 256 && reply.image.height() <= 256);
        QCOMPARE(reply.image.pixelColor(128, 128), QColor(Qt::red));
        const auto hash = reply.hash;
        reply = runPreview({path, hash}, {}, process());
        QVERIFY(reply.ok);
        QVERIFY(reply.cached);
        QVERIFY(reply.first_page);
        QVERIFY(reply.message.contains(QStringLiteral("PDF 第一頁")));
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), bytes);
        unchanged.close();
        write(path, pdfFixture("0 0 200 200", true));
        reply = runPreview({path, hash}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!reply.cached);
        QVERIFY(reply.hash != hash);
        QCOMPARE(reply.image.pixelColor(128, 128), QColor(Qt::red));
        write(path, pdfFixture("0 0 200 200", false, true));
        reply = runPreview({path, {}}, {}, process());
        QVERIFY(!reply.ok);
        QVERIFY2(reply.message.contains(QStringLiteral("Encrypted")), qPrintable(reply.message));
        write(path, pdfFixture("0 0 1000000 1000000"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, "%PDF-1.4\ncorrupt");
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, {});
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, QByteArray(8 * 1024 * 1024 + 1, 'x'));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        std::stop_source stopped;
        stopped.request_stop();
        QVERIFY(!runPreview({path, {}}, stopped.get_token(), process()).ok);
        QVERIFY(!parsePreview("{\"ok\":true,\"kind\":\"cached\",\"first_page\":\"true\"}").ok);
        QVERIFY(!parsePreview(
                     "{\"ok\":true,\"kind\":\"cached\",\"first_page\":true,\"first_frame\":true}")
                     .ok);
    }
    void bmpStaticPreviewLimitsAndCache() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("圖片 日本語.BMP")));
        QImage image(16, 8, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "BMP"));
        QCOMPARE(bytes.left(2), QByteArray("BM"));
        write(path, bytes);
        auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!reply.first_frame && !reply.first_page);
        QCOMPARE(reply.image.size(), QSize(256, 128));
        QCOMPARE(reply.image.pixelColor(128, 64), QColor(Qt::red));
        const auto hash = reply.hash;
        reply = runPreview({path, hash}, {}, process());
        QVERIFY(reply.ok && reply.cached);
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), bytes);
        unchanged.close();
        auto top_down = bytes;
        top_down.replace(22, 4, QByteArray::fromHex("f8ffffff"));
        write(path, top_down);
        reply = runPreview({path, hash}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.image.size(), QSize(256, 128));
        QCOMPARE(reply.image.pixelColor(128, 64), QColor(Qt::red));
        image.fill(Qt::blue);
        QVERIFY(image.save(path, "BMP"));
        reply = runPreview({path, hash}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!reply.cached && reply.hash != hash);
        QCOMPARE(reply.image.pixelColor(128, 64), QColor(Qt::blue));
        auto oversized = bytes;
        oversized.replace(18, 4, QByteArray::fromHex("01200000"));
        write(path, oversized);
        reply = runPreview({path, {}}, {}, process());
        QVERIFY(!reply.ok);
        QVERIFY2(reply.message.contains(QStringLiteral("limits")), qPrintable(reply.message));
        oversized = bytes;
        oversized.replace(18, 8, QByteArray::fromHex("0110000001100000"));
        write(path, oversized);
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        oversized = bytes;
        oversized.replace(22, 4, QByteArray::fromHex("ffffff7f"));
        write(path, oversized);
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        oversized.replace(22, 4, QByteArray::fromHex("00000080"));
        write(path, oversized);
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, bytes.left(26));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, {});
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, QByteArray(8 * 1024 * 1024 + 1, 'x'));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        std::stop_source stopped;
        stopped.request_stop();
        QVERIFY(!runPreview({path, {}}, stopped.get_token(), process()).ok);
    }
    void pdfAspectRatioAndGeometryBounds() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("橫向.PDF")));
        write(path, pdfFixture("0 0 200 100"));
        const auto reply = runPreview({path, {}}, {}, process());
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QCOMPARE(reply.image.size(), QSize(256, 256));
        QCOMPARE(reply.image.pixelColor(128, 64), QColor(Qt::red));
        QCOMPARE(reply.image.pixelColor(128, 192), QColor(Qt::white));
        write(path, pdfFixture("0 0 14400 14400"));
        QVERIFY(runPreview({path, {}}, {}, process()).ok);
        write(path, pdfFixture("0 0 14401 14400"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
        write(path, pdfFixture("0 0 0.001 0.001"));
        QVERIFY(!runPreview({path, {}}, {}, process()).ok);
    }
    void pdfWindowOptInAndVisibilityLifecycle() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("視窗首頁.pdf")));
        const auto bytes = pdfFixture();
        write(path, bytes);
        SearchWindow window([path](const auto &, const auto &) {
            return std::vector<fileweaver::SearchResult>{
                {1, 1, path.toUtf8().toStdString(), "preview.pdf", 0}};
        });
        window.show();
        window.setDatabase(QStringLiteral("C:/synthetic/pdf.sqlite"));
        control<QLineEdit>(window, "query")->setText(QStringLiteral("preview"));
        QTRY_COMPARE(control<QTreeView>(window, "results")->model()->rowCount(), 1);
        auto *enabled = control<QCheckBox>(window, "previewEnabled");
        auto *image = control<QLabel>(window, "previewImage");
        auto *status = control<QLabel>(window, "previewStatus");
        QVERIFY(!enabled->isChecked());
        QVERIFY(image->pixmap().isNull());
        enabled->setChecked(true);
        QTRY_VERIFY(!image->pixmap().isNull());
        QVERIFY(status->text().contains(QStringLiteral("PDF 第一頁")));
        QCOMPARE(image->pixmap().toImage().pixelColor(image->pixmap().width() / 2,
                                                      image->pixmap().height() / 2),
                 QColor(Qt::red));
        window.hide();
        QTRY_VERIFY(!enabled->isChecked());
        QVERIFY(image->pixmap().isNull());
        auto changed = bytes;
        changed.replace("1 0 0 rg", "0 0 1 rg");
        write(path, changed);
        window.show();
        QVERIFY(!enabled->isChecked());
        enabled->setChecked(true);
        QTRY_VERIFY(!image->pixmap().isNull());
        QCOMPARE(image->pixmap().toImage().pixelColor(image->pixmap().width() / 2,
                                                      image->pixmap().height() / 2),
                 QColor(Qt::blue));
        QVERIFY(status->text().contains(QStringLiteral("PDF 第一頁")));
        QTest::mouseClick(control<QPushButton>(window, "previewClear"), Qt::LeftButton);
        QVERIFY(image->pixmap().isNull());
        QFile unchanged(path);
        QVERIFY(unchanged.open(QIODevice::ReadOnly));
        QCOMPARE(unchanged.readAll(), changed);
        window.close();
    }
    void previewProcessProtocolTimeoutAndCancellation() {
        const auto worker = process("preview_test_worker.exe");
        auto reply = runPreview({QStringLiteral("C:\\synthetic\\normal.txt"), {}}, {}, worker);
        QVERIFY2(reply.ok, qPrintable(reply.message));
        QVERIFY(!runPreview({QStringLiteral("C:\\synthetic\\malformed.txt"), {}}, {}, worker).ok);
        QVERIFY(!runPreview({QStringLiteral("C:\\synthetic\\oversize.txt"), {}}, {}, worker).ok);
        QVERIFY(!runPreview({QStringLiteral("C:\\synthetic\\crash.txt"), {}}, {}, worker).ok);
        auto fast = worker;
        std::int64_t tick = 0;
        fast.now = [&] {
            return std::chrono::steady_clock::time_point(std::chrono::seconds(++tick));
        };
        reply = runPreview({QStringLiteral("C:\\synthetic\\slow.txt"), {}}, {}, fast);
        QVERIFY(!reply.ok);
        QVERIFY(reply.message.contains(QStringLiteral("5 秒")));
        QSemaphore entered;
        std::stop_source cancel;
        auto slow = worker;
        slow.now = [&] {
            entered.release();
            return std::chrono::steady_clock::now();
        };
        std::jthread task([&] {
            reply = runPreview({QStringLiteral("C:\\synthetic\\slow.txt"), {}}, cancel.get_token(),
                               slow);
        });
        QTRY_VERIFY(entered.available() > 0);
        cancel.request_stop();
        task.join();
        QVERIFY(!reply.ok);
        QVERIFY(reply.message.contains(QStringLiteral("取消")));
        QVERIFY(!parsePreview("{\"ok\":true,\"kind\":\"image\",\"width\":99999}").ok);
        QVERIFY(!parsePreview(QByteArray(512 * 1024 + 1, 'x')).ok);
    }
    void previewLatestRequestAndClearInvalidateCache() {
        std::atomic<int> calls = 0;
        QString last_hash;
        PreviewController reader([&](const auto &request, const auto &) {
            last_hash = request.cached_hash;
            ++calls;
            PreviewReply value;
            value.ok = true;
            value.hash = QString(64, u'a');
            value.image = QImage(2, 2, QImage::Format_RGBA8888);
            value.image.fill(Qt::red);
            return value;
        });
        QSignalSpy completed(&reader, &PreviewController::completed);
        reader.submit(QStringLiteral("C:\\one.png"));
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(last_hash.isEmpty());
        reader.submit(QStringLiteral("C:\\one.png"));
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(!last_hash.isEmpty());
        reader.clear();
        reader.submit(QStringLiteral("C:\\one.png"));
        QTRY_COMPARE(completed.count(), 3);
        QVERIFY(last_hash.isEmpty());
        QCOMPARE(calls.load(), 3);
        std::atomic<bool> cancelled = false;
        std::atomic<bool> entered = false;
        PreviewController staged([&](const auto &request, const auto &stop) {
            if (request.path.contains(QStringLiteral("old"))) {
                entered = true;
                std::mutex mutex;
                std::condition_variable_any ready;
                std::unique_lock lock(mutex);
                ready.wait(lock, stop, [] { return false; });
                cancelled = true;
            }
            PreviewReply value;
            value.ok = true;
            value.text = request.path;
            return value;
        });
        QSignalSpy replies(&staged, &PreviewController::completed);
        staged.submit(QStringLiteral("old"));
        QTRY_VERIFY(entered.load());
        staged.submit(QStringLiteral("new"));
        QTRY_COMPARE(replies.count(), 1);
        QVERIFY(cancelled.load());
        QCOMPARE(qvariant_cast<PreviewReply>(replies.at(0).at(0)).text, QStringLiteral("new"));
    }
    void preferencesAreBoundedOptionalAndExcludeActivation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath(QStringLiteral("preferences.json"));
        QVERIFY(readPreferences(path).ok);
        QVERIFY(!QFile::exists(path));
        Preferences value;
        value.enabled = true;
        value.fuzzy.weights.path = 77;
        value.hotkey = QStringLiteral("Ctrl+Shift+Space");
        value.font = 14;
        value.mode = 6;
        QVERIFY(writePreferences(path, value).ok);
        auto loaded = readPreferences(path);
        QVERIFY(loaded.ok);
        QCOMPARE(loaded.value.fuzzy.weights.path, 77);
        QCOMPARE(loaded.value.font, 14);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        file.close();
        QVERIFY(!bytes.contains("database"));
        QVERIFY(!bytes.contains("preview"));
        QVERIFY(!bytes.contains("watch"));
        QVERIFY(!bytes.contains("query"));
        value.enabled = false;
        QVERIFY(writePreferences(path, value).ok);
        loaded = readPreferences(path);
        QVERIFY(loaded.ok);
        QVERIFY(!loaded.value.enabled);
        write(path, "{invalid");
        QVERIFY(!readPreferences(path).ok);
        write(path, QByteArray(4097, 'x'));
        QVERIFY(!readPreferences(path).ok);
        value.enabled = true;
        value.font = 100;
        QVERIFY(!writePreferences(path, value).ok);
        value.font = 12;
        {
            PreferencesController writer;
            writer.save(path, value);
        }
        QVERIFY(readPreferences(path).ok);
        QCOMPARE(readPreferences(path).value.font, 12);
        QVERIFY(!writePreferences(temp.path(), value).ok);
    }
    void filtersModesAndPreferenceOptInUseSharedRequest() {
        std::mutex mutex;
        fileweaver::SearchRequest last;
        std::atomic<int> calls = 0;
        SearchWindow window([&](const auto &request, const auto &) {
            const std::lock_guard lock(mutex);
            last = request;
            ++calls;
            return result();
        });
        window.show();
        window.setDatabase(QStringLiteral("C:/synthetic/index.sqlite"));
        control<QComboBox>(window, "mode")->setCurrentIndex(6);
        control<QLineEdit>(window, "query")->setText(QStringLiteral("img proc fin"));
        QTRY_VERIFY(calls.load() > 0);
        {
            const std::lock_guard lock(mutex);
            QVERIFY(last.mode == fileweaver::SearchMode::fuzzy);
            QVERIFY(last.fuzzy.path_components);
        }
        QTest::mouseClick(control<QPushButton>(window, "filterSettingsButton"), Qt::LeftButton);
        auto *dialog = window.findChild<QDialog *>(QStringLiteral("filterSettings"));
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>(QStringLiteral("filterValue0"))
            ->setText(QStringLiteral("5"));
        dialog->findChild<QLineEdit *>(QStringLiteral("filterValue1"))
            ->setText(QStringLiteral("99"));
        dialog->findChild<QLineEdit *>(QStringLiteral("filterValue2"))
            ->setText(QStringLiteral("C:\\synthetic"));
        dialog->findChild<QCheckBox *>(QStringLiteral("filterExtensionEnabled"))->setChecked(true);
        dialog->findChild<QLineEdit *>(QStringLiteral("filterValue5"))
            ->setText(QStringLiteral("txt"));
        dialog->findChild<QSpinBox *>(QStringLiteral("filterLimit"))->setValue(12);
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        const auto previous = calls.load();
        QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        QTRY_VERIFY(calls.load() > previous);
        {
            const std::lock_guard lock(mutex);
            QCOMPARE(last.limit, std::size_t{12});
            QCOMPARE(last.filters.minimum_size, std::optional<std::int64_t>{5});
            QCOMPARE(last.filters.extension, std::optional<std::string>{"txt"});
        }
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = temp.filePath(QStringLiteral("prefs.json"));
        window.loadPreferences(path);
        auto *remember = control<QCheckBox>(window, "rememberPreferences");
        QTRY_VERIFY(remember->isEnabled());
        QVERIFY(!QFile::exists(path));
        QTest::mouseClick(remember, Qt::LeftButton);
        QTRY_VERIFY(QFile::exists(path));
        QTRY_VERIFY(readPreferences(path).ok);
        control<QSpinBox>(window, "interfaceFontSize")->setValue(15);
        QTRY_COMPARE(readPreferences(path).value.font, 15);
        SearchWindow restored([](const auto &, const auto &) { return result(); });
        restored.loadPreferences(path);
        restored.show();
        QTRY_COMPARE(control<QSpinBox>(restored, "interfaceFontSize")->value(), 15);
        QVERIFY(!control<QCheckBox>(restored, "hotkeyEnabled")->isChecked());
        QVERIFY(!control<QCheckBox>(restored, "previewEnabled")->isChecked());
        QVERIFY(!control<QPushButton>(restored, "watchStart")->isEnabled());
    }
    void escapeHidesOnlyWithEnabledHotkeyAndXStops() {
        HotkeyApi api{[](const auto &) { return 0UL; }, [](int) { return 0UL; }};
        SearchWindow window([](const auto &, const auto &) { return result(); }, nullptr, api);
        window.show();
        window.activateWindow();
        QTest::mouseClick(control<QCheckBox>(window, "hotkeyEnabled"), Qt::LeftButton);
        QVERIFY(control<QCheckBox>(window, "hotkeyEnabled")->isChecked());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY(!window.isVisible());
        QVERIFY(control<QCheckBox>(window, "hotkeyEnabled")->isChecked());
        window.show();
        window.close();
        QVERIFY(!control<QCheckBox>(window, "hotkeyEnabled")->isChecked());
    }
    void windowPreviewRequiresOptInAndClearsOnIndexChange() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto path = native(temp.filePath(QStringLiteral("preview.cpp")));
        write(path, "int synthetic = 42;");
        SearchWindow window([path](const auto &, const auto &) {
            return std::vector<fileweaver::SearchResult>{
                {1, 1, path.toUtf8().toStdString(), "preview.cpp", 19}};
        });
        window.show();
        window.setDatabase(QStringLiteral("C:/synthetic/one.sqlite"));
        control<QLineEdit>(window, "query")->setText(QStringLiteral("preview"));
        auto *view = control<QTreeView>(window, "results");
        QTRY_COMPARE(view->model()->rowCount(), 1);
        auto *enabled = control<QCheckBox>(window, "previewEnabled");
        auto *text = control<QPlainTextEdit>(window, "previewText");
        QVERIFY(!enabled->isChecked());
        QVERIFY(text->toPlainText().isEmpty());
        enabled->setChecked(true);
        QTRY_COMPARE(text->toPlainText(), QStringLiteral("int synthetic = 42;"));
        QTest::mouseClick(control<QPushButton>(window, "previewClear"), Qt::LeftButton);
        QVERIFY(text->toPlainText().isEmpty());
        enabled->setChecked(false);
        enabled->setChecked(true);
        QTRY_VERIFY(!text->toPlainText().isEmpty());
        window.setDatabase(QStringLiteral("C:/synthetic/two.sqlite"));
        QVERIFY(text->toPlainText().isEmpty());
        enabled->setChecked(false);
        window.close();
    }
    void syntaxHighlightingDoesNotInterpretText() {
        QPlainTextEdit editor;
        PreviewHighlighter highlighter(editor.document());
        editor.setPlainText(QStringLiteral("int main() { return 1; }\n<script>anything</script>"));
        highlighter.rehighlight();
        QVERIFY(!editor.document()->firstBlock().layout()->formats().isEmpty());
        QVERIFY(editor.toPlainText().contains(QStringLiteral("<script>")));
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    if (QApplication::platformName() != QStringLiteral("minimal")) {
        return 2;
    }
    Phase2Tests tests;
    return QTest::qExec(&tests, QApplication::arguments());
}
#include "phase2_test.moc"
