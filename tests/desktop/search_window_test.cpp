#include "arguments.hpp"
#include "fileweaver/application/index.hpp"
#include "fileweaver/application/initialize.hpp"
#include "fileweaver/application/inspect.hpp"
#include "search_controller.hpp"
#include "search_window.hpp"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

using fileweaver::desktop::SearchController;
using fileweaver::desktop::SearchReply;
using fileweaver::desktop::SearchWindow;

namespace {
template <class T> T *control(SearchWindow &window, const char *name) {
    auto *found = window.findChild<T *>(QString::fromLatin1(name));
    if (!found) {
        throw std::runtime_error("Required desktop control missing");
    }
    return found;
}
fileweaver::SearchResult record(const std::string &name) {
    return {1, 1, "C:/synthetic/" + name, name, 3};
}
} // namespace

class DesktopTests final : public QObject {
    Q_OBJECT
  private slots:
    void startupArguments() {
        QCommandLineParser parser;
        fileweaver::desktop::configureArguments(parser);
        QVERIFY(parser.parse({QStringLiteral("fileweaver-desktop")}));
        QVERIFY(!parser.isSet(QStringLiteral("database")));
        const auto database = QStringLiteral("C:/合成資料 日本語😀/索引.sqlite");
        QVERIFY(parser.parse(
            {QStringLiteral("fileweaver-desktop"), QStringLiteral("--database"), database}));
        QCOMPARE(parser.value(QStringLiteral("database")), database);
        QVERIFY(
            !parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--database")}));
        QVERIFY(!parser.errorText().isEmpty());
        QVERIFY(!parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--unknown")}));
        QVERIFY(parser.parse({QStringLiteral("fileweaver-desktop"), QStringLiteral("--help")}));
        QVERIFY(parser.isSet(QStringLiteral("help")));
        QVERIFY(parser.helpText().contains(QStringLiteral("--database")));
    }
    void doesNotSearchImplicitly() {
        std::atomic<int> calls = 0;
        SearchWindow window([&](const auto &, const auto &) {
            ++calls;
            return std::vector<fileweaver::SearchResult>{};
        });
        window.show();
        auto *query = control<QLineEdit>(window, "query");
        query->setText(QStringLiteral("demo"));
        QTest::qWait(230);
        QCOMPARE(calls.load(), 0);
        query->clear();
        window.setDatabase(QStringLiteral("C:/synthetic/never-opened.sqlite"));
        QTest::qWait(230);
        QCOMPARE(calls.load(), 0);
        QVERIFY(!control<QPushButton>(window, "copyPath")->isEnabled());
    }

    void unicodeModesKeyboardCopyAndOfflineSnapshot() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto base = std::filesystem::path(temporary.path().toStdWString());
        const auto root = base / std::filesystem::path(u8"\u8CC7\u6599 source");
        std::filesystem::create_directory(root);
        const std::vector<std::u8string> names{u8"\u5831\u544A_\u65E5\u672C\u8A9E\U0001F600.txt",
                                               u8"x\u5831\u544A_\u65E5\u672C\u8A9E.txt",
                                               u8"notes.md"};
        for (const auto &name : names) {
            std::ofstream file(root / std::filesystem::path(name));
            file << "abc";
        }
        const auto database = base / std::filesystem::path(u8"\u7D22\u5F15\U0001F600.sqlite");
        fileweaver::initialize_database(database);
        QCOMPARE(
            fileweaver::index_directory({database, root, {}}, [] { return std::int64_t{1}; }).added,
            std::int64_t{3});
        const auto before = fileweaver::catalog_status(database);
        const auto selected = QString::fromStdWString(
            std::filesystem::canonical(root / std::filesystem::path(names[0])).native());
        SearchWindow window;
        window.setDatabase(QString::fromStdWString(database.native()));
        window.show();
        window.activateWindow();
        auto *query = control<QLineEdit>(window, "query");
        auto *tree = control<QTreeView>(window, "results");
        auto *mode = control<QComboBox>(window, "mode");
        query->setText(QStringLiteral("報告"));
        QTRY_COMPARE(tree->model()->rowCount(), 2);
        query->setFocus();
        QTest::keyClick(query, Qt::Key_Down);
        QVERIFY(tree->hasFocus());
        QCOMPARE(tree->currentIndex().row(), 0);
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(tree->currentIndex().row(), 1);
        QTest::mouseClick(control<QPushButton>(window, "copyPath"), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(selected));
        QApplication::clipboard()->setText(QStringLiteral("synthetic clipboard sentinel"));
        tree->setFocus();
        QTest::keyClick(tree, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(selected));
        mode->setCurrentIndex(1);
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        QCOMPARE(tree->model()->index(0, 0).data().toString(), QStringLiteral("報告_日本語😀.txt"));
        mode->setCurrentIndex(2);
        query->setText(QStringLiteral("報告_日本語😀.txt"));
        QTRY_COMPARE(tree->model()->rowCount(), 1);
        query->clear();
        QCOMPARE(tree->model()->rowCount(), 0);
        QVERIFY(!control<QPushButton>(window, "copyPath")->isEnabled());
        std::filesystem::rename(root, base / "offline-source");
        mode->setCurrentIndex(0);
        query->setText(QStringLiteral("報告"));
        QTRY_COMPARE(tree->model()->rowCount(), 2);
        const auto after = fileweaver::catalog_status(database);
        QCOMPARE(after.scan_records, before.scan_records);
        QCOMPARE(after.present_files, before.present_files);
        QTest::keyClick(query, Qt::Key_Escape);
        QTRY_VERIFY(!window.isVisible());
    }

    void missingDatabaseIsAnErrorWithoutCreation() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto database = temporary.path() + QStringLiteral("/missing.sqlite");
        SearchWindow window;
        window.setDatabase(database);
        window.show();
        control<QLineEdit>(window, "query")->setText(QStringLiteral("report"));
        QTRY_VERIFY(
            control<QLabel>(window, "status")->text().startsWith(QStringLiteral("無法搜尋：")));
        QCOMPARE(control<QTreeView>(window, "results")->model()->rowCount(), 0);
        QVERIFY(!std::filesystem::exists(std::filesystem::path(database.toStdWString())));
    }

    void latestPendingWinsAndGuiStaysResponsive() {
        struct State {
            QSemaphore gate;
            std::atomic<bool> entered = false;
            std::atomic<bool> cancelled = false;
            std::atomic<int> active = 0;
            std::atomic<bool> overlapped = false;
            std::mutex mutex;
            std::vector<std::string> calls;
        };
        auto state = std::make_shared<State>();
        SearchController controller([state](const auto &request, const auto &stop) {
            if (state->active.fetch_add(1) != 0) {
                state->overlapped = true;
            }
            {
                const std::lock_guard lock(state->mutex);
                state->calls.push_back(request.query);
            }
            if (request.query == "old") {
                state->entered = true;
                state->gate.acquire(); // Deliberately return a stale success after cancellation.
                state->cancelled = stop.stop_requested();
            }
            state->active.fetch_sub(1);
            return std::vector<fileweaver::SearchResult>{record(request.query)};
        });
        const auto release = qScopeGuard([state] { state->gate.release(); });
        QSignalSpy completed(&controller, &SearchController::completed);
        controller.submit({{}, "old"});
        QTRY_VERIFY(state->entered.load());
        bool heartbeat = false;
        QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        controller.submit({{}, "middle"});
        controller.submit({{}, "latest"});
        state->gate.release();
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = qvariant_cast<SearchReply>(completed.at(0).at(0));
        QCOMPARE(reply.results.size(), std::size_t{1});
        QCOMPARE(QString::fromStdString(reply.results[0].name), QStringLiteral("latest"));
        QVERIFY(state->cancelled.load());
        QVERIFY(!state->overlapped.load());
        const std::lock_guard lock(state->mutex);
        QVERIFY(state->calls == (std::vector<std::string>{"old", "latest"}));
    }

    void cancellationOnDestructionAndInvalidUtf8() {
        std::atomic<bool> entered = false;
        std::atomic<bool> exited = false;
        auto controller = std::make_unique<SearchController>([&](const auto &, const auto &stop) {
            std::mutex mutex;
            std::condition_variable_any condition;
            std::unique_lock lock(mutex);
            entered = true;
            condition.wait(lock, stop, [] { return false; });
            exited = true;
            return std::vector<fileweaver::SearchResult>{};
        });
        controller->submit({{}, "waiting"});
        QTRY_VERIFY(entered.load());
        controller.reset();
        QVERIFY(exited.load());
        SearchController invalid([](const auto &, const auto &) {
            return std::vector<fileweaver::SearchResult>{record(std::string("\xff", 1))};
        });
        QSignalSpy completed(&invalid, &SearchController::completed);
        invalid.submit({{}, "invalid"});
        QTRY_COMPARE(completed.count(), 1);
        const auto reply = qvariant_cast<SearchReply>(completed.at(0).at(0));
        QVERIFY(!reply.error.isEmpty());
        QVERIFY(reply.results.empty());
    }
};

int main(int argc, char *argv[]) {
    qputenv("QT_QPA_PLATFORM", "minimal");
    QApplication application(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    if (QApplication::platformName() != QStringLiteral("minimal")) {
        return 2; // Never run clipboard assertions against the user's native clipboard.
    }
    DesktopTests tests;
    return QTest::qExec(&tests, QApplication::arguments());
}

#include "search_window_test.moc"
