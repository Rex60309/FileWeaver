#include "../support/directory_link.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include "index_controller.hpp"
#include "search_window.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <atomic>
#include <fstream>
#include <memory>
#include <windows.h>
using namespace fileweaver::desktop;
namespace {
std::filesystem::path native(const QString &path) {
    return std::filesystem::path(path.toStdWString());
}
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error("Cannot write generated fixture");
    }
}
QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot read generated fixture");
    }
    return file.readAll();
}
template <class T> T *control(SearchWindow &window, const char *name) {
    auto *found = window.findChild<T *>(QString::fromLatin1(name));
    if (!found) {
        throw std::runtime_error("Missing onboarding control");
    }
    return found;
}
std::unique_ptr<SearchWindow> windowFor(const QString &managed, IndexApi api = windowsIndexApi()) {
    return std::make_unique<SearchWindow>(SearchController::Search{}, nullptr, windowsHotkeyApi(),
                                          openSelectedFile, revealSelectedFile, windowsWatchApi(),
                                          std::move(api), managed);
}
void attemptSearch(const fileweaver::SearchRequest &request, const QString &managed) {
    static_cast<void>(desktopSearchFiles(request, {}, managed));
}
void attemptIndex(const fileweaver::IndexRequest &request,
                  const std::function<std::int64_t()> &clock, const std::stop_token &stop) {
    static_cast<void>(buildDesktopIndex(request, clock, stop));
}
QDialog *confirmation(SearchWindow &window) {
    auto *dialog = window.findChild<QDialog *>(QStringLiteral("indexConfirmation"));
    if (!dialog) {
        throw std::runtime_error("Missing confirmation");
    }
    return dialog;
}
void accept(SearchWindow &window) {
    auto *buttons = confirmation(window)->findChild<QDialogButtonBox *>(
        QStringLiteral("indexConfirmationButtons"));
    if (!buttons) {
        throw std::runtime_error("Missing confirmation buttons");
    }
    QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
}
} // namespace
class OnboardingTests final : public QObject {
    Q_OBJECT
  private slots:
    void missingManagedCatalogIsReadOnlyAndCorruptionIsAnError() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto managed = temp.filePath(QStringLiteral("managed/index.sqlite"));
        fileweaver::SearchRequest request{native(managed), "*"};
        request.mode = fileweaver::SearchMode::glob;
        QVERIFY(desktopSearchFiles(request, {}, managed).empty());
        QVERIFY(!QFile::exists(managed));
        QVERIFY(!QDir(temp.filePath(QStringLiteral("managed"))).exists());
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptSearch(request, QStringLiteral("C:/different.sqlite")));
        QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("managed"))));
        write(managed, "unrelated contents");
        QVERIFY_THROWS_EXCEPTION(std::exception, attemptSearch(request, managed));
        QCOMPARE(read(managed), QByteArray("unrelated contents"));
    }
    void nativeCreationUnicodeAndSavedPolicy() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.filePath(QStringLiteral("Source 日本語😀"));
        QVERIFY(QDir().mkpath(root + QStringLiteral("/ignore")));
        write(root + QStringLiteral("/報告.txt"), "unchanged source");
        write(root + QStringLiteral("/movie.mp4"), QByteArray("binary\0fixture", 14));
        write(root + QStringLiteral("/ignore/hidden.txt"), "excluded");
        const auto managed = temp.filePath(QStringLiteral("catalog/index.sqlite"));
        fileweaver::IndexRequest request{native(managed), native(root), {}};
        request.scan.excluded_paths.emplace_back("ignore");
        const auto clock = [] { return std::int64_t{123}; };
        const auto indexed = buildDesktopIndex(request, clock, {});
        QCOMPARE(indexed.added, std::int64_t{2});
        QVERIFY(!indexed.missing_reconciled);
        auto roots = fileweaver::list_roots(native(managed));
        QCOMPARE(roots.size(), std::size_t{1});
        QCOMPARE(roots[0].exclusions.size(), std::size_t{1});
        QCOMPARE(read(root + QStringLiteral("/報告.txt")), QByteArray("unchanged source"));
        QCOMPARE(fileweaver::catalog_status(native(managed)).present_files, std::int64_t{2});
        request.root = native(root.toUpper());
        request.scan = {};
        static_cast<void>(buildDesktopIndex(request, clock, {}));
        QCOMPARE(fileweaver::list_roots(native(managed)).size(), std::size_t{1});
        QCOMPARE(fileweaver::list_roots(native(managed))[0].exclusions.size(), std::size_t{1});
        QCOMPARE(fileweaver::catalog_status(native(managed)).present_files, std::int64_t{2});
    }
    void storageRootGuardsAndCancellation() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(root));
        write(root + QStringLiteral("/file.txt"), "original");
        const auto clock = [] { return std::int64_t{1}; };
        const auto inside = root + QStringLiteral("/catalog/index.sqlite");
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(inside), native(root), {}}, clock, {}));
        QVERIFY(!QFile::exists(inside));
        const auto managed = temp.filePath(QStringLiteral("managed/index.sqlite"));
        QVERIFY_THROWS_EXCEPTION(
            std::exception,
            attemptIndex({native(managed), native(root + QStringLiteral("/missing")), {}}, clock,
                         {}));
        QVERIFY_THROWS_EXCEPTION(
            std::exception,
            attemptIndex({native(managed), native(root + QStringLiteral("/file.txt")), {}}, clock,
                         {}));
        QVERIFY_THROWS_EXCEPTION(
            std::exception,
            attemptIndex({native(managed), native(QStringLiteral("C:")), {}}, clock, {}));
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(managed), native(root), {}}, {}, {}));
        QVERIFY(!QDir(temp.filePath(QStringLiteral("managed"))).exists());
        std::stop_source cancelled;
        cancelled.request_stop();
        QVERIFY_THROWS_EXCEPTION(std::exception, attemptIndex({native(managed), native(root), {}},
                                                              clock, cancelled.get_token()));
        QVERIFY(!QFile::exists(managed));
        const auto linked = temp.filePath(QStringLiteral("linked-source"));
        fileweaver::test::directoryLink({native(root), native(linked)});
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(managed), native(linked), {}}, clock, {}));
        QVERIFY(!QFile::exists(managed));
        QVERIFY(QDir().mkpath(temp.filePath(QStringLiteral("managed"))));
        write(managed, "not a FileWeaver database");
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(managed), native(root), {}}, clock, {}));
        QCOMPARE(read(managed), QByteArray("not a FileWeaver database"));
        const auto original = temp.filePath(QStringLiteral("existing.sqlite"));
        fileweaver::initialize_database(native(original));
        const auto before = read(original);
        const auto alias = temp.filePath(QStringLiteral("alias.sqlite"));
        std::filesystem::create_hard_link(native(original), native(alias));
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(alias), native(root), {}}, clock, {}));
        QCOMPARE(read(original), before);
        QCOMPARE(read(root + QStringLiteral("/file.txt")), QByteArray("original"));
    }
    void emptyCatalogRecoveryAndStraySidecarsAreProtected() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(root));
        write(root + QStringLiteral("/sample.txt"), "untouched");
        const auto managed = temp.filePath(QStringLiteral("index.sqlite"));
        const auto clock = [] { return std::int64_t{1}; };
        write(managed + QStringLiteral("-wal"), "unrelated sidecar");
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(managed), native(root), {}}, clock, {}));
        QVERIFY(!QFile::exists(managed));
        QCOMPARE(read(managed + QStringLiteral("-wal")), QByteArray("unrelated sidecar"));
        QVERIFY(QFile::remove(managed + QStringLiteral("-wal")));
        write(managed, {});
        QCOMPARE(buildDesktopIndex({native(managed), native(root), {}}, clock, {}).added,
                 std::int64_t{1});
        const auto before = read(managed);
        const auto attrs = GetFileAttributesW(native(managed).c_str());
        QVERIFY(attrs != INVALID_FILE_ATTRIBUTES);
        QVERIFY(SetFileAttributesW(native(managed).c_str(), attrs | FILE_ATTRIBUTE_READONLY));
        const auto restore =
            qScopeGuard([&] { SetFileAttributesW(native(managed).c_str(), attrs); });
        QVERIFY_THROWS_EXCEPTION(std::exception,
                                 attemptIndex({native(managed), native(root), {}}, clock, {}));
        QCOMPARE(read(managed), before);
    }
    void workerCancellationBusyAndErrors() {
        std::atomic<bool> entered = false;
        IndexApi api{
            [&](const auto &, const auto &clock, const auto &stop) -> fileweaver::IndexSummary {
                if (clock() != 7) {
                    throw std::runtime_error("Wrong clock");
                }
                entered = true;
                std::mutex mutex;
                std::condition_variable_any ready;
                std::unique_lock lock(mutex);
                ready.wait(lock, stop, [] { return false; });
                throw std::runtime_error("cancelled fixture");
            },
            [] { return std::int64_t{7}; }};
        IndexController controller(api);
        QSignalSpy completed(&controller, &IndexController::completed);
        QVERIFY(controller.submit({{}, {}, {}}));
        QVERIFY(controller.isBusy());
        QVERIFY(!controller.submit({{}, {}, {}}));
        QTRY_VERIFY(entered.load());
        bool heartbeat = false;
        QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        controller.cancel();
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = qvariant_cast<IndexReply>(completed[0][0]);
        QVERIFY(reply.cancelled && !reply.ok);
        QVERIFY(!controller.isBusy());
        IndexController failure(
            {[](const auto &, const auto &, const auto &) -> fileweaver::IndexSummary {
                 throw std::runtime_error("synthetic storage failure");
             },
             [] { return std::int64_t{1}; }});
        QSignalSpy failed(&failure, &IndexController::completed);
        QVERIFY(failure.submit({{}, {}, {}}));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(qvariant_cast<IndexReply>(failed[0][0])
                    .message.contains(QStringLiteral("synthetic storage failure")));
        IndexController partial({[](const auto &, const auto &, const auto &) {
                                     fileweaver::IndexSummary summary;
                                     summary.scan.complete = false;
                                     summary.scan.errors = 1;
                                     return summary;
                                 },
                                 [] { return std::int64_t{1}; }});
        QSignalSpy incomplete(&partial, &IndexController::completed);
        QVERIFY(partial.submit({{}, {}, {}}));
        QTRY_COMPARE(incomplete.count(), 1);
        const auto incomplete_reply = qvariant_cast<IndexReply>(incomplete[0][0]);
        QVERIFY(!incomplete_reply.ok && !incomplete_reply.cancelled);
        QVERIFY(incomplete_reply.message.contains(QStringLiteral("掃描未完成")));
    }
    void cancelledNativeScanRetainsFilesAndIncompleteScanDoesNotCommit() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.filePath(QStringLiteral("source"));
        QVERIFY(QDir().mkpath(root));
        write(root + QStringLiteral("/original.txt"), "original");
        const auto managed = temp.filePath(QStringLiteral("index.sqlite"));
        const auto clock = [] { return std::int64_t{1}; };
        static_cast<void>(buildDesktopIndex({native(managed), native(root), {}}, clock, {}));
        write(root + QStringLiteral("/new.txt"), "new file");
        std::stop_source stop;
        const auto cancelled = buildDesktopIndex(
            {native(managed), native(root), {}},
            [&] {
                stop.request_stop();
                return std::int64_t{2};
            },
            stop.get_token());
        QVERIFY(cancelled.scan.cancelled);
        QCOMPARE(fileweaver::catalog_status(native(managed)).present_files, std::int64_t{1});
        QVERIFY(QDir().mkpath(root + QStringLiteral("/nested")));
        write(root + QStringLiteral("/nested/child.txt"), "nested file");
        const auto partial = temp.filePath(QStringLiteral("partial.sqlite"));
        fileweaver::IndexRequest request{native(partial), native(root), {}};
        request.scan.maximum_depth = 1;
        const auto incomplete = buildDesktopIndex(request, clock, {});
        QVERIFY(!incomplete.scan.complete);
        QCOMPARE(fileweaver::catalog_status(native(partial)).present_files, std::int64_t{0});
        QCOMPARE(read(root + QStringLiteral("/original.txt")), QByteArray("original"));
    }
    void applicationLogoIsInherited() {
        const auto previous = QApplication::windowIcon();
        const auto restore = qScopeGuard([previous] { QApplication::setWindowIcon(previous); });
        QPixmap pixels(16, 16);
        pixels.fill(Qt::blue);
        const QIcon icon(pixels);
        QApplication::setWindowIcon(icon);
        SearchWindow window;
        QCOMPARE(window.windowIcon().cacheKey(), icon.cacheKey());
    }
    void confirmedCreationBrowsesAndReopeningDoesNotRescan() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto root = temp.filePath(QStringLiteral("加入 日本語"));
        QVERIFY(QDir().mkpath(root));
        for (int i = 0; i < 140; ++i) {
            write(root + QStringLiteral("/file%1.txt").arg(i, 3, 10, QChar(u'0')), "metadata only");
        }
        const auto managed = temp.filePath(QStringLiteral("managed/index.sqlite"));
        const auto existing = temp.filePath(QStringLiteral("existing.sqlite"));
        fileweaver::initialize_database(native(existing));
        const auto before = read(existing);
        auto window = windowFor(managed);
        window->show();
        window->setDatabase(existing);
        QTRY_VERIFY(control<QPushButton>(*window, "addFolder")->isEnabled());
        window->requestIndexFolder(root);
        const QPointer<QDialog> dialog = confirmation(*window);
        QCOMPARE(dialog->findChild<QPlainTextEdit *>(QStringLiteral("indexRoot"))->toPlainText(),
                 QDir::toNativeSeparators(root));
        QVERIFY(!QFile::exists(managed));
        QTest::keyClick(dialog.data(), Qt::Key_Return); // Cancel is the default.
        QTRY_VERIFY(dialog.isNull() || !dialog->isVisible());
        QVERIFY(!QFile::exists(managed));
        QTRY_VERIFY(window->findChild<QDialog *>(QStringLiteral("indexConfirmation")) == nullptr);
        window->requestIndexFolder(root);
        accept(*window);
        QTRY_VERIFY(!control<QPushButton>(*window, "cancelIndex")->isEnabled());
        QTRY_COMPARE(control<QTreeView>(*window, "results")->model()->rowCount(), 100);
        QVERIFY(control<QTreeView>(*window, "results")->viewport()->height() >= 150);
        QVERIFY(control<QLineEdit>(*window, "query")->text().isEmpty());
        QVERIFY(
            control<QLabel>(*window, "indexStatus")->text().contains(QStringLiteral("新增 140")));
        QVERIFY(!control<QCheckBox>(*window, "previewEnabled")->isChecked());
        QVERIFY(!control<QCheckBox>(*window, "hotkeyEnabled")->isChecked());
        const auto capture = qEnvironmentVariable("FILEWEAVER_ONBOARDING_CAPTURE");
        if (!capture.isEmpty()) {
            QVERIFY(window->grab().save(capture));
        }
        QCOMPARE(read(existing), before);
        QCOMPARE(fileweaver::catalog_status(native(managed)).present_files, std::int64_t{140});
        control<QLineEdit>(*window, "query")->setText(QStringLiteral("file139"));
        QTRY_COMPARE(control<QTreeView>(*window, "results")->model()->rowCount(), 1);
        control<QLineEdit>(*window, "query")->clear();
        QTRY_COMPARE(control<QTreeView>(*window, "results")->model()->rowCount(), 100);
        window->close();
        window.reset();
        const auto scans = fileweaver::catalog_status(native(managed)).scan_records;
        write(root + QStringLiteral("/not-yet-indexed.txt"), "new source");
        auto reopened = windowFor(managed);
        reopened->show();
        reopened->setDatabase(managed);
        QTRY_COMPARE(control<QTreeView>(*reopened, "results")->model()->rowCount(), 100);
        QCOMPARE(fileweaver::catalog_status(native(managed)).scan_records, scans);
    }
    void cancellationKeepsPreviousIndexAndBlocksCompetingActions() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const auto existing = temp.filePath(QStringLiteral("existing.sqlite"));
        fileweaver::initialize_database(native(existing));
        const auto before = read(existing);
        const auto managed = temp.filePath(QStringLiteral("managed/index.sqlite"));
        std::atomic<bool> entered = false;
        IndexApi api{[&](const auto &, const auto &, const auto &stop) -> fileweaver::IndexSummary {
                         entered = true;
                         std::mutex mutex;
                         std::condition_variable_any ready;
                         std::unique_lock lock(mutex);
                         ready.wait(lock, stop, [] { return false; });
                         throw std::runtime_error("cancelled fixture");
                     },
                     [] { return std::int64_t{1}; }};
        auto window = windowFor(managed, api);
        window->show();
        window->setDatabase(existing);
        QTRY_VERIFY(control<QPushButton>(*window, "addFolder")->isEnabled());
        window->requestIndexFolder(temp.path());
        accept(*window);
        QTRY_VERIFY(entered.load());
        QVERIFY(control<QProgressBar>(*window, "indexProgress")->isVisible());
        QVERIFY(!control<QPushButton>(*window, "addFolder")->isEnabled());
        QVERIFY(!control<QPushButton>(*window, "chooseDatabase")->isEnabled());
        QVERIFY(!control<QLineEdit>(*window, "query")->isEnabled());
        QVERIFY(!control<QPushButton>(*window, "watchStart")->isEnabled());
        window->setDatabase(QStringLiteral("C:/should-not-switch.sqlite"));
        QTest::mouseClick(control<QPushButton>(*window, "cancelIndex"), Qt::LeftButton);
        QTRY_VERIFY(!control<QProgressBar>(*window, "indexProgress")->isVisible());
        QVERIFY(control<QLineEdit>(*window, "query")->isEnabled());
        QVERIFY(control<QLabel>(*window, "indexStatus")->text().contains(QStringLiteral("已取消")));
        QCOMPARE(control<QLineEdit>(*window, "databasePath")->text(),
                 QDir::toNativeSeparators(existing));
        QCOMPARE(read(existing), before);
        QVERIFY(!QFile::exists(managed));
    }
};
int main(int argc, char **argv) {
    const bool capture = qEnvironmentVariableIsSet("FILEWEAVER_ONBOARDING_CAPTURE");
    qputenv("QT_QPA_PLATFORM", capture ? "minimal:enable_fonts" : "minimal");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    if (capture) {
        QApplication::setStyle(QStringLiteral("Fusion"));
        QApplication::setPalette(QApplication::style()->standardPalette());
    }
    QApplication::setQuitOnLastWindowClosed(false);
    OnboardingTests tests;
    return QTest::qExec(&tests, QApplication::arguments());
}
#include "onboarding_test.moc"
